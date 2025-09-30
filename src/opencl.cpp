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
#include "opencl.h"
#include "cl_kernels.h"

#include <iostream>
#include <utility>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>


const PixelFormat PixelFormat::RGBA8 = PixelFormat(4, 1, true, CV_8UC4, {CL_RGBA, CL_UNSIGNED_INT8});
const PixelFormat PixelFormat::U8 = PixelFormat(1, 1, false, CV_8UC1, {CL_R, CL_UNSIGNED_INT8});
const PixelFormat PixelFormat::F32 = PixelFormat(4, 1, false, CV_32FC1, {CL_R, CL_FLOAT});
const PixelFormat PixelFormat::NV12 = PixelFormat(1, 2, true, CV_8UC1, {CL_R, CL_UNSIGNED_INT8}); //Do not use as OpenCL image format or with OpenCV, intended for usage with RTPStreamer (TODO overallocated, actual necessary size is just 3/2)

const PixelFormat PixelFormat::RGGB8 = PixelFormat(2, 2, true, CV_8UC1, {CL_R, CL_UNSIGNED_INT8}, "-DRGGB");
const PixelFormat PixelFormat::GRBG8 = PixelFormat(2, 2, true, CV_8UC1, {CL_R, CL_UNSIGNED_INT8}, "-DGRBG");
const PixelFormat PixelFormat::BGR8 = PixelFormat(3, 1, true, CV_8UC3, {CL_RGB, CL_UNSIGNED_INT8}, "-DBGR"); //Do not use as OpenCL image format, CL_RGB seldomly supported by hardware


OpenCL::OpenCL() {
	std::vector<cl::Platform> platforms;
	cl::Platform::get(&platforms);

	if (platforms.empty()) {
		std::cerr << "[OpenCL] No platforms found. Check OpenCL installation!" << std::endl;
		exit(1);
	}

	if(!searchDevice(platforms, CL_DEVICE_TYPE_GPU) && !searchDevice(platforms, CL_DEVICE_TYPE_ALL)) {
		std::cerr << "[OpenCL] No GPU devices found. Check OpenCL installation!" << std::endl;
		exit(1);
	}

	context = cl::Context(device);
	cl::Context::setDefault(context);
	queue = cl::CommandQueue(context, device);
	cl::CommandQueue::setDefault(queue);
}

bool OpenCL::searchDevice(const std::vector<cl::Platform>& platforms, cl_device_type type) {
	for(const cl::Platform& platform : platforms) {
		std::vector<cl::Device> devices;
		platform.getDevices(type, &devices);

		for(cl::Device& d : devices) {
			device = d;
			std::cout << "[OpenCL] Using device: " << platform.getInfo<CL_PLATFORM_NAME>() << " » " << device.getInfo<CL_DEVICE_NAME>() << std::endl;
			return true;
		}
	}

	return false;
}

cl::Kernel OpenCL::compile(const char *code, const std::string &options) {
	cl::Program::Sources sources;
	sources.emplace_back(code);

	cl::Program program(context, sources);
	if (program.build({device}, options.c_str()) != CL_SUCCESS) {
		std::cerr << "[OpenCL] Error during kernel compilation: " << program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << std::endl;
		exit(1);
	}

	std::vector<cl::Kernel> kernels;
	int error = program.createKernels(&kernels);
	if(error != CL_SUCCESS) {
		std::cerr << "[OpenCL] Error during kernel creation: " << error << std::endl;
		exit(1);
	}
	if(kernels.empty()) {
		std::cerr << "[OpenCL] Kernel missing: " << code << std::endl;
		exit(1);
	}
	return kernels[0];
}

void OpenCL::wait(const cl::Event& event) {
	int error = event.wait();
	if(error != CL_SUCCESS) {
		std::cerr << "[OpenCL] Error during kernel execution: " << error << std::endl;
		exit(1);
	}
}

//Pool design adapted from Jonathan Mee https://stackoverflow.com/a/27828584 CC BY-SA 3.0
std::shared_ptr<CLImage> OpenCL::acquire(const PixelFormat* format, int width, int height, const std::string& name) {
	std::vector<std::shared_ptr<CLImage>>& formatPool = pool[format];

	auto iterator = std::find_if(formatPool.begin(), formatPool.end(), [&](const std::shared_ptr<CLImage>& i){
		return i.use_count() == 1 && i->width == width && i->height == height;
	});
	if(iterator != formatPool.end()) {
		(*iterator)->name = name;
		return *iterator;
	}

	auto array = std::make_shared<CLImage>(format, width, height, name);
	formatPool.push_back(array);
	return array;
}

std::shared_ptr<RawImage> OpenCL::acquireNV12(int width, int height) {
	auto iterator = std::find_if(nv12pool.begin(), nv12pool.end(), [&](const std::shared_ptr<RawImage>& i){
		return i.use_count() == 1 && i->width == width && i->height == height;
	});
	if(iterator != nv12pool.end()) {
		return *iterator;
	}

	auto array = std::make_shared<RawImage>(&PixelFormat::NV12, width, height);
	nv12pool.push_back(array);
	return array;
}

static inline cl::Buffer clAlloc(cl_mem_flags type, cl::size_type size, void* data) {
	int error;
	cl::Buffer buffer(type | CL_MEM_READ_WRITE, size, data, &error);
	if(error != CL_SUCCESS) {
		std::cerr << "[OpenCL] Error during image allocation: " << error << std::endl;
		exit(1);
	}
	return buffer;
}

CLArray::CLArray(int size): buffer(clAlloc((cl_mem_flags) CL_MEM_ALLOC_HOST_PTR, (cl::size_type) size, nullptr)), size(size) {}
CLArray::CLArray(void* data, const int size): buffer(clAlloc((cl_mem_flags) CL_MEM_COPY_HOST_PTR, (cl::size_type) size, data)), size(size) {}

static inline cl::Image2D allocImage(int width, int height, const PixelFormat* format) {
	int error;
	cl::Image2D image = cl::Image2D(cl::Context::getDefault(), CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, format->clFormat, width, height, 0, nullptr, &error);
	if(error != CL_SUCCESS) {
		std::cerr << "[OpenCL] Image creation error: " << error << " " << width << "," << height << " " << (format == &PixelFormat::RGBA8) << std::endl;
		exit(1);
	}
	return image;
}

CLImage::CLImage(const PixelFormat* format): format(format), width(0), height(0) {}
CLImage::CLImage(const PixelFormat* format, int width, int height, std::string name): image(allocImage(width, height, format)), format(format), width(width), height(height), name(std::move(name)) {}

void CLImage::save(const std::string &suffix, float factor, float offset) const {
	if(format == &PixelFormat::F32) {
		cv::Mat grayscale(height, width, CV_8UC1);
		CLImageMap<float> map = read<float>();
		for(int y = 0; y < height; y++) {
			for(int x = 0; x < width; x++) {
				grayscale.data[x + width * y] = cv::saturate_cast<uint8_t>(factor * map[x + width * y] + offset);
			}
		}

		cv::imwrite("img/" + name + suffix, grayscale);
	} else if(format == &PixelFormat::RGBA8) {
		cv::Mat bgr;
		cv::cvtColor(read<RGBA>().cv, bgr, cv::COLOR_RGBA2BGR);
		cv::imwrite("img/" + name + suffix, bgr);
	} else {
		cv::imwrite("img/" + name + suffix, read<RGBA>().cv);
	}
}