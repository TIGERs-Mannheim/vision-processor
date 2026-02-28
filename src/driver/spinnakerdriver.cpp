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
#ifdef SPINNAKER

#include "spinnakerdriver.h"
#include <cstdlib> 
#include <algorithm>
#include <iostream>
#include <unistd.h>

#define CATCH_SPINNAKER(f) try { f; } catch (Spinnaker::Exception &e) { std::cerr << "[Spinnaker] Could not set parameter: " << e.GetFullErrorMessage() << std::endl; }

constexpr int    kMinBufferCount = 3;      // Minimum buffer count required for NewestOnly mode
constexpr size_t kUsb3PacketSize = 1024;   // USB3 packet size alignment requirement
constexpr size_t kPageSize       = 4096;   // Page size alignment requirement for Meteor Lake

static size_t alignUp(size_t size, size_t alignment) {
	return ((size + alignment - 1) / alignment) * alignment;
}

class SpinnakerImage : public RawImage {
public:
	SpinnakerImage(SpinnakerDriver& source, const Spinnaker::ImagePtr& pImage): RawImage(*source.borrow(pImage)), source(source), pImage(pImage) {
		timestamp = (double)pImage->GetTimeStamp() / 1e9;
	}

	~SpinnakerImage() override {
		pImage->Release();
		source.restore(*this);
	}

private:
	SpinnakerDriver& source;
	const Spinnaker::ImagePtr pImage;
};

SpinnakerDriver::SpinnakerDriver(const CameraConfig& config) {
	pSystem = Spinnaker::System::GetInstance();

	while(true) {
		Spinnaker::CameraList camList = pSystem->GetCameras();
		if (camList.GetSize() > config.hardwareId) {
			pCam = camList.GetByIndex(config.hardwareId);
			pCam->Init();
			std::cout << "[Spinnaker] Opened " << pCam->DeviceModelName.GetValue() << " - " << pCam->DeviceSerialNumber.GetValue().c_str() << std::endl;
			camList.Clear();
			break;
		}

		std::cerr << "[Spinnaker] Waiting for cam: " << camList.GetSize() << "/" << (config.hardwareId+1) << std::endl;

		camList.Clear();
		sleep(1);
	}

	// Reset camera parameters
	CATCH_SPINNAKER(pCam->UserSetDefault.SetValue(Spinnaker::UserSetDefault_Default))
	CATCH_SPINNAKER(pCam->UserSetSelector.SetValue(Spinnaker::UserSetSelector_Default))

	CATCH_SPINNAKER(pCam->TriggerMode.SetValue(Spinnaker::TriggerMode_Off))
	CATCH_SPINNAKER(pCam->AcquisitionMode.SetValue(Spinnaker::AcquisitionMode_Continuous))
	CATCH_SPINNAKER(pCam->PixelFormat.SetValue(Spinnaker::PixelFormat_BayerRG8))
	CATCH_SPINNAKER(pCam->AcquisitionFrameRateEnable.SetValue(false))

	if(config.autoResolution()) {
		CATCH_SPINNAKER(pCam->Width.SetValue(pCam->WidthMax.GetValue()));
		CATCH_SPINNAKER(pCam->Height.SetValue(pCam->HeightMax.GetValue()));
	} else {
		CATCH_SPINNAKER(pCam->Width.SetValue(config.width));
		CATCH_SPINNAKER(pCam->Height.SetValue(config.height));
	}

	if(config.autoExposure()) {
		CATCH_SPINNAKER(pCam->AutoExposureMeteringMode.SetValue(Spinnaker::AutoExposureMeteringMode_Average))
		CATCH_SPINNAKER(pCam->ExposureAuto.SetValue(Spinnaker::ExposureAuto_Continuous))
	} else {
		CATCH_SPINNAKER(pCam->ExposureAuto.SetValue(Spinnaker::ExposureAuto_Off))
		CATCH_SPINNAKER(pCam->ExposureTime.SetValue(config.exposure * 1000.0))
	}

	if(config.autoGain()) {
		CATCH_SPINNAKER(pCam->GainAuto.SetValue(Spinnaker::GainAuto_Continuous))
	} else {
		CATCH_SPINNAKER(pCam->GainAuto.SetValue(Spinnaker::GainAuto_Off))
		CATCH_SPINNAKER(pCam->Gain.SetValue(config.gain))
	}

	if(config.autoExposure() && config.autoGain()) {
		CATCH_SPINNAKER(pCam->AutoExposureControlPriority.SetValue(Spinnaker::AutoExposureControlPriority_Gain))
	}

	if(config.autoGamma()) {
		CATCH_SPINNAKER(pCam->GammaEnable.SetValue(false))
	} else {
		CATCH_SPINNAKER(pCam->GammaEnable.SetValue(true))
		CATCH_SPINNAKER(pCam->Gamma.SetValue((float)config.gamma))
	}

	if(config.whiteBalanceType != WhiteBalanceType_Manual) {
		CATCH_SPINNAKER(pCam->BalanceWhiteAuto.SetValue(Spinnaker::BalanceWhiteAuto_Continuous))
		CATCH_SPINNAKER(pCam->BalanceWhiteAutoProfile.SetValue(
				config.whiteBalanceType == WhiteBalanceType_AutoOutdoor
				? Spinnaker::BalanceWhiteAutoProfile_Outdoor
				: Spinnaker::BalanceWhiteAutoProfile_Indoor
		))
	} else {
		CATCH_SPINNAKER(pCam->BalanceWhiteAuto.SetValue(Spinnaker::BalanceWhiteAuto_Off))
		CATCH_SPINNAKER(pCam->BalanceRatioSelector.SetValue(Spinnaker::BalanceRatioSelector_Blue))
		CATCH_SPINNAKER(pCam->BalanceRatio.SetValue(config.whiteBalanceBlue))
		CATCH_SPINNAKER(pCam->BalanceRatioSelector.SetValue(Spinnaker::BalanceRatioSelector_Red))
		CATCH_SPINNAKER(pCam->BalanceRatio.SetValue(config.whiteBalanceRed))
	}

	pCam->TLStream.StreamBufferHandlingMode.SetValue(Spinnaker::StreamBufferHandlingMode_NewestOnly);

	// NewestOnly mode requires at least kMinBufferCount buffers
	int requiredBuffers = std::max(kMinBufferCount, (int)pCam->TLStream.StreamBufferCountManual.GetMin());
	pCam->TLStream.StreamBufferCountManual.SetValue(requiredBuffers);

	// Provide image buffers to achieve faster mapping with OpenCL
	int width  = (int)pCam->Width.GetValue();
	int height = (int)pCam->Height.GetValue();
	size_t rawSize = (size_t)width * height;

	// Align to both USB3 packet and Page size for Meteor Lake stability/decoding
	finalBufferSize_ = alignUp(alignUp(rawSize, kUsb3PacketSize), kPageSize);

	std::vector<void*> bufferPtrs;
	for(int i = 0; i < requiredBuffers; i++) {
		void* alignedPtr = nullptr;
		if (posix_memalign(&alignedPtr, kPageSize, finalBufferSize_) != 0) {
			throw std::runtime_error("[Spinnaker] posix_memalign failed");
		}
		
		// Track pointer for explicit free() in destructor
		m_allocatedPtrs.push_back(alignedPtr);
		
		auto buffer = std::make_shared<RawImage>(&PixelFormat::RGGB8, width/2, height/2, (unsigned char*)alignedPtr);
		
		// Create OpenCL mapping
		buffers[buffer] = std::make_unique<CLMap<uint8_t>>(buffer->write<uint8_t>());
		
		bufferPtrs.push_back(alignedPtr);
	}

	// Register user-owned buffers with the camera
	pCam->SetBufferOwnership(Spinnaker::SPINNAKER_BUFFER_OWNERSHIP_USER);
	pCam->SetUserBuffers(bufferPtrs.data(), bufferPtrs.size(), finalBufferSize_);

	if (IsWritable(pCam->GevSCPSPacketSize)) {
		CATCH_SPINNAKER(pCam->GevSCPSPacketSize.SetValue(9000));
	}

	pCam->BeginAcquisition();
}

std::shared_ptr<RawImage> SpinnakerDriver::readImage() {
	return std::make_shared<SpinnakerImage>(*this, pCam->GetNextImage());
}

const PixelFormat SpinnakerDriver::format() {
	return PixelFormat::RGGB8;
}

double SpinnakerDriver::expectedFrametime() {
	return 1 / pCam->AcquisitionResultingFrameRate.GetValue();
}

SpinnakerDriver::~SpinnakerDriver() {
	if (pCam) {
		pCam->EndAcquisition();
		pCam->DeInit();
	}

	// Explicitly free memory to prevent leaks
	for (void* ptr : m_allocatedPtrs) {
		if (ptr) free(ptr);
	}
	m_allocatedPtrs.clear();
}
std::shared_ptr<RawImage> SpinnakerDriver::borrow(const Spinnaker::ImagePtr& pImage) {
	void* data = pImage->GetData();

	auto it = m_fastBufferPool.find(data);
	if (it != m_fastBufferPool.end() && it->second.clMap != nullptr) {
		it->second.clMap = nullptr; // 使用中にマーク
		return it->second.image;
	}

	return std::make_shared<RawImage>(&PixelFormat::RGGB8, (int)pImage->GetWidth() / 2, (int)pImage->GetHeight() / 2, (unsigned char*)data);
}

void SpinnakerDriver::restore(const RawImage& image) {
	for (auto& item : buffers) {
		if(item.first->buffer == image.buffer) {
			item.second = std::make_unique<CLMap<uint8_t>>(item.first->write<uint8_t>());
			return;
		}
	}
}

#endif