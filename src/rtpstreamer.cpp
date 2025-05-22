/*
     Copyright 2024 Felix Weinmann

     Licensed under the Apache License, Version 2.0 (the "License");
     you may not use this file except in compliance with the License.
     You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

     Unless required by applicable law or agreed to in writing, software
     distributed under the License is distributed on an "AS IS" BASIS,
     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
     See the License for the specific language governing permissions and
     limitations under the License.
 */
#include "rtpstreamer.h"

#include <cstdlib>
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}


RTPStreamer::RTPStreamer(bool active, std::string uri, int framerate): active(active), uri(std::move(uri)), framerate(framerate), frametime_us(1000000 / framerate) {
	encoder = std::thread(&RTPStreamer::encoderRun, this);
}

RTPStreamer::~RTPStreamer() {
	stopEncoding = true;

	{
		std::unique_lock<std::mutex> lock(queueMutex);
		queueSignal.notify_one();
	}

	encoder.join();
	freeResources();
}

//Adopted from CC BY-SA 4.0 https://stackoverflow.com/a/61988145 Dmitrii Zabotlin
void RTPStreamer::sendFrame(std::shared_ptr<RawImage> image) {
	if(!active)
		return;

	std::unique_lock<std::mutex> lock(queueMutex);

	queue = std::move(image);
	queueSignal.notify_one();
}

//Adopted from CC BY-SA 3.0 https://stackoverflow.com/q/40825300 DankMemes and https://stackoverflow.com/q/46352604 Gaulois94
void RTPStreamer::allocResources() {
	if(codecCtx != nullptr)
		return;

	const AVCodec* codec;
	std::vector<const char*> codecNames {"h264_nvenc", "h264_qsv", "h264_vaapi", "libx264"};
	for (const auto &codecName : codecNames) {
		codec = avcodec_find_encoder_by_name(codecName);
		if(codec == nullptr)
			continue;

		codecCtx = avcodec_alloc_context3(codec);

		codecCtx->bit_rate = 3500000;
		codecCtx->width = width;
		codecCtx->height = height;
		codecCtx->time_base.num = 1;
		codecCtx->time_base.den = framerate;
		codecCtx->gop_size = framerate;
		codecCtx->max_b_frames = 0;
		codecCtx->pix_fmt = AV_PIX_FMT_NV12;
		codecCtx->codec_type = AVMEDIA_TYPE_VIDEO;

		if(strcmp(codecName, "h264_qsv") == 0) {
			av_opt_set(codecCtx->priv_data, "preset", "veryfast", 0);
		}
		if(strcmp(codecName, "libx264") == 0) {
			av_opt_set(codecCtx->priv_data, "preset", "ultrafast", 0);
			av_opt_set(codecCtx->priv_data, "tune", "zerolatency", 0);
		}

		if(avcodec_open2(codecCtx, codec, nullptr) == 0)
			break;

		av_free(codecCtx);
		codecCtx = nullptr;
	}

	if(codecCtx == nullptr) {
		std::cerr << "[RtpStreamer] Failed to find suitable encoder." << std::endl;
		exit(1);
	}

	std::cout << "[RtpStreamer] Using codec: " << codec->long_name << std::endl;

	fmtCtx  = avformat_alloc_context();
	auto* avFormat = (AVOutputFormat*)av_guess_format("rtp", nullptr, nullptr);
	avformat_alloc_output_context2(&fmtCtx, avFormat, avFormat->name, uri.c_str());
	avio_open(&fmtCtx->pb, uri.c_str(), AVIO_FLAG_WRITE);

	stream = avformat_new_stream(fmtCtx, codec);
	avcodec_parameters_from_context(stream->codecpar, codecCtx);
	stream->time_base.num = codecCtx->time_base.num;
	stream->time_base.den = codecCtx->time_base.den;

	int write = avformat_write_header(fmtCtx, nullptr);
	if(write < 0) {
		std::cerr << "[RtpStreamer] Failed to write header: " << write << std::endl;
		exit(1);
	}

	frame = av_frame_alloc();
	frame->format = codecCtx->pix_fmt;
	frame->width  = codecCtx->width;
	frame->height = codecCtx->height;
	frame->linesize[0] = width;
	frame->linesize[1] = width;

	pkt = av_packet_alloc();
}

void RTPStreamer::freeResources() {
	if(codecCtx != nullptr) {
		avcodec_send_frame(codecCtx, nullptr);
		avcodec_free_context(&codecCtx);
	}

	if(fmtCtx != nullptr) {
		avio_close(fmtCtx->pb);
		avformat_free_context(fmtCtx);
		fmtCtx = nullptr;
	}

	if(frame != nullptr) {
		av_frame_free(&frame);
		frame = nullptr;
	}

	if(pkt != nullptr) {
		av_packet_free(&pkt);
		pkt = nullptr;
	}
}

void RTPStreamer::encoderRun() {
	while(!stopEncoding) {
		std::shared_ptr<RawImage> image;
		{
			std::unique_lock<std::mutex> lock(queueMutex);
			while(queue == nullptr && !stopEncoding)
				queueSignal.wait(lock, [&]() { return queue != nullptr || stopEncoding; });

			if(stopEncoding) {
				freeResources();
				return;
			}

			image = queue;
			queue = nullptr;
		}

		if(image->width != width || image->height != height) {
			freeResources();
			width = image->width;
			height = image->height;
		}

		allocResources();

		auto startTime = std::chrono::high_resolution_clock::now();

		{
			CLMap<uint8_t> data = image->read<uint8_t>();
			frame->pts = currentFrameId++;
			frame->data[0] = *data;
			frame->data[1] = *data + width*height;
			avcodec_send_frame(codecCtx, frame);
		}
		image = nullptr;

		int status = avcodec_receive_packet(codecCtx, pkt);
		if(status == 0) {
			av_packet_rescale_ts(pkt, codecCtx->time_base, stream->time_base);
			av_interleaved_write_frame(fmtCtx, pkt);
			av_packet_unref(pkt);  // reset packet
		} else if(status == AVERROR(EAGAIN)) {
			// Encoder needs some more frames
		} else {
			std::cerr << "[RtpStreamer] Encoder error: " << status << std::endl;
		}

		std::this_thread::sleep_for(std::chrono::microseconds(frametime_us) - (std::chrono::high_resolution_clock::now() - startTime));
	}
}
