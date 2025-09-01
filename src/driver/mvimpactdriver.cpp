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
#ifdef MVIMPACT

#include "mvimpactdriver.h"
#include <mvIMPACT_CPP/mvIMPACT_acquire_GenICam.h>


class MVImpactImage : public RawImage {
public:
	explicit MVImpactImage(const std::shared_ptr<Request>& request): RawImage(&PixelFormat::GRBG8, request->imageWidth.read() / 2, request->imageHeight.read() / 2, (double)request->infoTimeStamp_us.read() / 1e6, (uint8_t*)request->imageData.read()), request(request) {}

private:
	std::shared_ptr<Request> request;
};


MVImpactDriver::MVImpactDriver(const CameraConfig& config) {
	while(devMgr.deviceCount() <= config.hardwareId) {
		std::cerr << "[mvIMPACT] Waiting for cam: " << devMgr.deviceCount() << "/" << (config.hardwareId+1) << std::endl;
		sleep(1);
		devMgr.updateDeviceList();
	}
	device = devMgr[config.hardwareId];
	device->userControlledImageProcessingEnable.write(TBoolean::bFalse);

	try {
		device->open();
	} catch(mvIMPACT::acquire::ImpactAcquireException& e) {
		std::cerr << "[mvIMPACT] Error while opening the camera: " << e.getErrorCodeAsString() << " " << e.getErrorString() << std::endl;
		exit(1);
	}

	SettingsBlueFOX settings(device);
	settings.cameraSetting.restoreDefault();
	settings.imageProcessing.restoreDefault();
	settings.imageDestination.restoreDefault();
	settings.cameraSetting.pixelFormat.write(TImageBufferPixelFormat::ibpfMono8);
	settings.imageDestination.pixelFormat.write(TImageDestinationPixelFormat::idpfRaw);

	if(!config.autoResolution()) {
		settings.imageDestination.imageWidth.write(config.width);
		settings.imageDestination.imageHeight.write(config.height);
	}

	if(config.autoExposure()) {
		settings.cameraSetting.autoExposeControl.write(TAutoExposureControl::aecOn);
	} else {
		settings.cameraSetting.autoExposeControl.write(TAutoExposureControl::aecOff);
		settings.cameraSetting.expose_us.write((int)(config.exposure * 1000));
	}

	if(config.autoGain()) {
		settings.cameraSetting.autoGainControl.write(TAutoGainControl::agcOn);
	} else {
		settings.cameraSetting.autoGainControl.write(TAutoGainControl::agcOff);
		settings.cameraSetting.gain_dB.write(config.gain);
	}

	if(config.whiteBalanceType != WhiteBalanceType_Manual) {
		settings.imageProcessing.whiteBalanceCalibration.write(TWhiteBalanceCalibrationMode::wbcmNextFrame);
	} else {
		settings.imageProcessing.whiteBalanceCalibration.write(TWhiteBalanceCalibrationMode::wbcmOff);
		WhiteBalanceSettings& wb = settings.imageProcessing.getWBUserSetting(0);
		wb.restoreDefault();
		wb.blueGain.write(config.whiteBalanceBlue);
		wb.redGain.write(config.whiteBalanceRed);
		settings.imageProcessing.whiteBalance.write(TWhiteBalanceParameter::wbpUser1);
	}


	//TODO user supplied memory functionality to prevent copies https://wassets.balluff.com/documents/DRF_957352_AA_000/CaptureToUserMemory_8cpp-example.html

	provider = std::make_unique<mvIMPACT::acquire::helper::RequestProvider>(device);
	provider->acquisitionStart();
}

MVImpactDriver::~MVImpactDriver() {
	//delete fi;
	provider->acquisitionStop();
	device->close();
}

std::shared_ptr<RawImage> MVImpactDriver::readImage() {
	std::shared_ptr<Request> request = provider->waitForNextRequest();

	//Get only newest frame
	std::shared_ptr<Request> newerRequest;
	while((newerRequest = provider->waitForNextRequest(0)) != nullptr)
		request = newerRequest;

	if(!request->isOK()) {
		std::cerr << "[mvIMPACT] Error while acquiring image: " << request->requestResult.readS() << std::endl;
		return nullptr;
	}

	return std::make_shared<MVImpactImage>(request);
}

const PixelFormat MVImpactDriver::format() {
	return PixelFormat::GRBG8;
}

double MVImpactDriver::expectedFrametime() {
	return 1 / GenICam::AcquisitionControl(device).mvResultingFrameRate.read();
}

#endif

