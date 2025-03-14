/*
     Copyright 2025 Felix Weinmann

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
#ifdef DC1394

#include "cameradriver.h"
#include <dc1394/control.h>
#include <dc1394/conversions.h>

class DC1394Driver : public CameraDriver {
public:
	explicit DC1394Driver(unsigned int id, double exposure, double gain, double gamma, WhiteBalanceType wbType, const std::vector<double>& wbValues);
	~DC1394Driver() override;

	std::shared_ptr<RawImage> readImage() override;

	const PixelFormat format() override;

	double expectedFrametime() override;

private:
	dc1394_t* dc1394;
	dc1394camera_t* camera = nullptr;
	dc1394framerate_t dcfps;
	dc1394video_mode_t dcformat;
	dc1394featureset_t features;
	dc1394video_frame_t * frame;
};

#endif