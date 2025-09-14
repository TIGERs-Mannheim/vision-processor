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
#include "cameradriver.h"

#include "driver/opencvdriver.h"
#include "driver/spinnakerdriver.h"
#include "driver/mvimpactdriver.h"

#include <yaml-cpp/yaml.h>

double realTimeOffset = 0.0;
double getRealTime() {
	return (double)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() / 1e6 + realTimeOffset;
}

double CameraDriver::getTime() {
	return getRealTime();
}


CameraConfig::CameraConfig(const YAML::Node &cam) {
	driverType = cam["driver"].as<std::string>("SPINNAKER");

	hardwareId = cam["id"].as<int>(0);
	path = cam["path"].as<std::string>("/dev/video" + std::to_string(this->hardwareId));

	width = cam["width"].as<int>(0);
	height = cam["height"].as<int>(0);
	exposure = cam["exposure"].as<double>(0.0);
	gain = cam["gain"].as<double>(0.0);
	gamma = cam["gamma"].as<double>(1.0);

	const YAML::Node wb = cam["white_balance"].IsDefined() ? cam["white_balance"] : YAML::Node();
	if(wb.IsMap()) {
		whiteBalanceBlue = wb["blue"].as<double>(1.0);
		whiteBalanceRed = wb["red"].as<double>(1.0);
	} else {
		whiteBalanceType = (wb.as<std::string>("OUTDOOR") == "OUTDOOR")
				? WhiteBalanceType_AutoOutdoor
				: WhiteBalanceType_AutoIndoor;
	}
}

bool CameraConfig::autoResolution() const {
	return width == 0 || height == 0;
}

bool CameraConfig::autoExposure() const {
	return exposure == 0.0;
}

bool CameraConfig::autoGain() const {
	return gain == 0.0;
}

bool CameraConfig::autoGamma() const {
	return gamma == 1.0;
}


std::unique_ptr<CameraDriver> openCamera(const CameraConfig& config) {
#ifdef SPINNAKER
	if(config.driverType == "SPINNAKER")
		return std::make_unique<SpinnakerDriver>(config);
#endif

#ifdef MVIMPACT
	if(config.driverType == "MVIMPACT")
		return std::make_unique<MVImpactDriver>(config);
#endif

	if(config.driverType == "OPENCV")
		return std::make_unique<OpenCVDriver>(config);

	std::cerr << "[Resources] Unknown camera/image driver defined: " << config.driverType << std::endl;
	exit(1);
}
