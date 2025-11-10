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

#include <memory>
#include <yaml-cpp/node/node.h>
#include "opencl.h"

extern double realTimeOffset;
double getRealTime();


enum WhiteBalanceType {
	WhiteBalanceType_Manual,
	// Separate outdoor and indoor auto profiles for cameras with different auto algorithms (e.g. Spinnaker)
	WhiteBalanceType_AutoOutdoor,
	WhiteBalanceType_AutoIndoor
};


/** Abstract camera interface for the implementation of arbitrary camera interfaces. */
class CameraDriver {
public:
	virtual ~CameraDriver() = default;

	/** Method to await and get the next image. Updates lastFrame* times. */
	std::shared_ptr<RawImage> nextImage();

	virtual const PixelFormat format() = 0;

	virtual double expectedFrametime();

	// Bound to the driver for reproducibility during testing with files.
	virtual double getTime();

	/** Contains the timestamp of the last image returned by nextImage. */
	double lastFrameTimestamp = getTime();

protected:
	virtual std::shared_ptr<RawImage> readImage() = 0;

private:
	double lastFrameDelta = 1 / 30.0;
};


/** Wrapper for camera options contained in the cam section of config.yml. */
class CameraConfig {
public:
	CameraConfig(const YAML::Node& cam);

	bool autoResolution() const;
	bool autoExposure() const;
	bool autoGain() const;
	bool autoGamma() const;

	std::string driverType;

	int hardwareId;
	std::string path; // Used for OpenCV, with fallback to /dev/video{hardwareId}

	int width;
	int height;
	double exposure;
	double gain;
	double gamma;

	WhiteBalanceType whiteBalanceType = WhiteBalanceType_Manual;
	double whiteBalanceBlue;
	double whiteBalanceRed;
	double whiteBalance[2];
};


std::unique_ptr<CameraDriver> openCamera(const CameraConfig& config);
