/*
     Copyright 2026 Emiel Steerneman

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
#include "snapshotwriter.h"

#include <fstream>
#include "log.h"
#include <utility>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>


SnapshotWriter::SnapshotWriter() {
	worker = std::thread(&SnapshotWriter::run, this);
}

SnapshotWriter::~SnapshotWriter() {
	{
		std::lock_guard<std::mutex> lock(mu);
		stop = true;
	}
	signal.notify_one();
	worker.join();
}

void SnapshotWriter::offer(std::shared_ptr<CLImage> image, std::string path) {
	{
		std::lock_guard<std::mutex> lock(mu);
		pending[std::move(path)] = std::move(image);
	}
	signal.notify_one();
}

static bool encodeJpeg(const CLImage& image, std::vector<uchar>& encoded) {
	const std::vector<int> jpegParams = { cv::IMWRITE_JPEG_QUALITY, 85 };
	cv::Mat encodable;
	if(image.format == &PixelFormat::RGBA8) {
		cv::cvtColor(image.read<RGBA>().cv, encodable, cv::COLOR_RGBA2BGR);
	} else if(image.format == &PixelFormat::F32) {
		cv::convertScaleAbs(image.read<float>().cv, encodable, 1.0, 127.0);
	} else {
		WARN("unsupported pixel format");
		return false;
	}
	return cv::imencode(".jpg", encodable, encoded, jpegParams);
}

static void writeAtomic(const std::string& path, const std::vector<uchar>& bytes) {
	const std::string tmpPath = path + ".tmp";
	{
		std::ofstream out(tmpPath, std::ios::binary);
		if(!out) {
			WARN("open failed: " << tmpPath);
			return;
		}
		out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}
	if(std::rename(tmpPath.c_str(), path.c_str()) != 0) {
		WARN("rename failed: " << tmpPath << " -> " << path);
		std::remove(tmpPath.c_str());
	}
}

void SnapshotWriter::run() {
	while(true) {
		std::map<std::string, std::shared_ptr<CLImage>> batch;
		{
			std::unique_lock<std::mutex> lock(mu);
			signal.wait(lock, [&]() { return !pending.empty() || stop; });
			if(stop && pending.empty())
				return;
			batch.swap(pending);
		}

		for(auto& [path, image] : batch) {
			std::vector<uchar> encoded;
			try {
				if(!encodeJpeg(*image, encoded)) {
					WARN("encode failed: " << path);
					continue;
				}
			} catch(const cv::Exception& e) {
				WARN("encode error for " << path << ": " << e.what());
				continue;
			}
			writeAtomic(path, encoded);
		}
	}
}
