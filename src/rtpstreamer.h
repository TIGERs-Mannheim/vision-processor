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
#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>

#include "opencl.h"


typedef struct AVCodec AVCodec;
typedef struct AVCodecContext AVCodecContext;
typedef struct AVFormatContext AVFormatContext;
typedef struct AVFrame AVFrame;
typedef struct AVStream AVStream;
typedef struct AVPacket AVPacket;


class RTPStreamer {
public:
	explicit RTPStreamer(bool active, std::string uri, int framerate = 30);
	~RTPStreamer();
	void sendFrame(std::shared_ptr<RawImage> image);
private:
	void encoderRun();

	void allocResources();
	void freeResources();

	const bool active;
	const std::string uri;
	const int framerate;
	const int frametime_us;
	int width = 0;
	int height = 0;

	bool stopEncoding = false;
	std::thread encoder;

	std::shared_ptr<RawImage> queue = nullptr;
	std::mutex queueMutex = std::mutex();
	std::condition_variable queueSignal = std::condition_variable();
	long currentFrameId = 0;

	AVCodecContext* codecCtx = nullptr;
	AVFormatContext* fmtCtx = nullptr;
	AVStream* stream = nullptr;
	AVFrame* frame = nullptr;
	AVPacket* pkt = nullptr;
};
