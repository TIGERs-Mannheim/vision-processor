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

#include <opencv2/videoio.hpp>

#include "cameradriver.h"

class OpenCVDriver : public CameraDriver {
public:
	explicit OpenCVDriver(const std::string& path, double exposure, double gain, double gamma, WhiteBalanceType wbType, const std::vector<double>& wbValues);

	std::shared_ptr<RawImage> readImage() override;

	const PixelFormat format() override;

	double expectedFrametime() override;

	double getTime() override;

private:
	cv::VideoCapture capture;
	std::shared_ptr<RawImage> image = nullptr;
	std::string name;
};
