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

#include <cstdio>
#include <fstream>
#include <iostream>
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
	cv.notify_one();
	worker.join();
}

void SnapshotWriter::offer(std::shared_ptr<CLImage> image, std::string path) {
	{
		std::lock_guard<std::mutex> lock(mu);
		slot = std::move(image);
		slotPath = std::move(path);
	}
	cv.notify_one();
}

void SnapshotWriter::run() {
	const std::vector<int> jpegParams = { cv::IMWRITE_JPEG_QUALITY, 85 };

	while(true) {
		std::shared_ptr<CLImage> image;
		std::string path;
		{
			std::unique_lock<std::mutex> lock(mu);
			cv.wait(lock, [&]() { return slot != nullptr || stop; });
			if(stop && slot == nullptr)
				return;
			image = std::move(slot);
			path = std::move(slotPath);
			slot = nullptr;
		}

		std::vector<uchar> encoded;
		try {
			cv::Mat bgr;
			cv::cvtColor(image->read<RGBA>().cv, bgr, cv::COLOR_RGBA2BGR);
			if(!cv::imencode(".jpg", bgr, encoded, jpegParams)) {
				std::cerr << "[SnapshotWriter] imencode failed" << std::endl;
				continue;
			}
		} catch(const cv::Exception& e) {
			std::cerr << "[SnapshotWriter] encode failed: " << e.what() << std::endl;
			continue;
		}
		image = nullptr;

		const std::string tmpPath = path + ".tmp";
		{
			std::ofstream out(tmpPath, std::ios::binary);
			if(!out) {
				std::cerr << "[SnapshotWriter] open failed: " << tmpPath << std::endl;
				continue;
			}
			out.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
		}
		if(std::rename(tmpPath.c_str(), path.c_str()) != 0) {
			std::cerr << "[SnapshotWriter] rename failed: " << tmpPath << " -> " << path << std::endl;
			std::remove(tmpPath.c_str());
		}
	}
}
