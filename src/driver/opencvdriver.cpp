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
#include "opencvdriver.h"

OpenCVDriver::OpenCVDriver(const std::string &path, double exposure, double gain, double gamma, WhiteBalanceType wbType, const std::vector<double> &wbValues): capture(path, cv::CAP_ANY, {cv::CAP_PROP_HW_ACCELERATION, cv::VIDEO_ACCELERATION_ANY}), name(path) {
	std::replace(name.begin(), name.end(), '/', '_');

	// Use compressed data stream on USB2 cameras to unlock the highest resolution - framerate combination
	capture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
	capture.set(cv::CAP_PROP_FRAME_WIDTH, INT_MAX);
	capture.set(cv::CAP_PROP_FRAME_HEIGHT, INT_MAX);

	if(exposure == 0.0) {
		capture.set(cv::CAP_PROP_AUTO_EXPOSURE, 1.0);
	} else {
		capture.set(cv::CAP_PROP_AUTO_EXPOSURE, 0.0);
		capture.set(cv::CAP_PROP_EXPOSURE, exposure * 1000.0);
	}

	if(gain != 0.0) {
		capture.set(cv::CAP_PROP_GAIN, gain);
	}

	if(gamma != 1.0) {
		capture.set(cv::CAP_PROP_GAMMA, gamma);
	}

	if(wbType != WhiteBalanceType_Manual) {
		capture.set(cv::CAP_PROP_AUTO_WB, 1.0);
	} else {
		capture.set(cv::CAP_PROP_AUTO_WB, 0.0);
		capture.set(cv::CAP_PROP_WHITE_BALANCE_BLUE_U, wbValues[0]);
		capture.set(cv::CAP_PROP_WHITE_BALANCE_RED_V, wbValues[1]);
	}
}

std::shared_ptr<RawImage> OpenCVDriver::readImage() {
	if(image == nullptr || !image.unique())
		image = std::make_shared<RawImage>(&PixelFormat::BGR8, capture.get(cv::CAP_PROP_FRAME_WIDTH), capture.get(cv::CAP_PROP_FRAME_HEIGHT), name);

	CLMap<uint8_t> map = image->write<uint8_t>();
	cv::Mat mat(cv::Size(image->width, image->height), CV_8UC3, (void*)*map);
	if(!capture.read(mat))
		return nullptr;

	return image;
}

const PixelFormat OpenCVDriver::format() {
	return PixelFormat::BGR8;
}

double OpenCVDriver::expectedFrametime() {
	double fps = capture.get(cv::CAP_PROP_FPS);

	if(fps == 0.0) // Unavailable for cameras, estimate 30 FPS
		fps = 30.0;

	return 1 / fps;
}


double OpenCVDriver::getTime() {
	double pos = capture.get(cv::CAP_PROP_POS_FRAMES);

	if(pos == 0.0) // Not a video file, use real time
		return CameraDriver::getTime();

	return pos / capture.get(cv::CAP_PROP_FPS);
}
