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
#include <yaml-cpp/yaml.h>

#include "cl_kernels.h"
#include "Resources.h"
#include "driver/cameradriver.h"

template<>
struct YAML::convert<Eigen::Vector2f> {
	static YAML::Node encode(const Eigen::Vector2f& rhs) {
		YAML::Node node;
		node.push_back(rhs.x());
		node.push_back(rhs.y());
		return node;
	}

	static bool decode(const YAML::Node& node, Eigen::Vector2f& rhs) {
		if(!node.IsSequence() || node.size() != 2) {
			return false;
		}

		rhs.x() = node[0].as<float>();
		rhs.y() = node[1].as<float>();
		return true;
	}
};

template<>
struct YAML::convert<Eigen::Vector3i> {
	static YAML::Node encode(const Eigen::Vector3i& rhs) {
		YAML::Node node;
		node.push_back(rhs.x());
		node.push_back(rhs.y());
		node.push_back(rhs.z());
		return node;
	}

	static bool decode(const YAML::Node& node, Eigen::Vector3i& rhs) {
		if(!node.IsSequence() || node.size() != 3) {
			return false;
		}

		rhs.x() = node[0].as<int>();
		rhs.y() = node[1].as<int>();
		rhs.z() = node[2].as<int>();
		return true;
	}
};

static YAML::Node getOptional(const YAML::Node& node) {
	return node.IsDefined() ? node : YAML::Node();
}

Resources::Resources(const YAML::Node& config) {
	openCl = std::make_shared<OpenCL>();
	camera = openCamera(CameraConfig(getOptional(config["camera"])));

	camId = config["cam_id"].as<int>(0);
	if (camId < 0 || camId > 7) {
		std::cerr << "[Resources] Invalid camera ID, must be >= 0 and <= 7: " << camId << std::endl;
		exit(1);
	}

	YAML::Node thresholds = getOptional(config["thresholds"]);
	minCircularity = thresholds["circularity"].as<double>(15.0);
	minScore = thresholds["score"].as<double>(5.0);
	maxBlobs = thresholds["blobs"].as<int>(2000);
	minConfidence = thresholds["min_confidence"].as<float>(0.2f);
	minCamEdgeDistance = thresholds["min_cam_edge_distance"].as<double>(170.0);
	resamplingFactor = thresholds["resampling_factor"].as<float>(1.0f);

	YAML::Node tracking = getOptional(config["tracking"]);
	minTrackingRadius = tracking["min_tracking_radius"].as<double>(20.0);
	maxBotAcceleration = 1000 * tracking["max_bot_acceleration"].as<double>(6.5);

	YAML::Node geometry = getOptional(config["geometry"]);
	cameraAmount = geometry["camera_amount"].as<int>(1);
	cameraHeight = geometry["camera_height"].as<double>(0.0);
	lineCorners = geometry["line_corners"].as<std::vector<Eigen::Vector2f>>(std::vector<Eigen::Vector2f>());
	geometryRefinement = geometry["refinement"].as<bool>(true);
	fieldLineThreshold = geometry["field_line_threshold"].as<int>(5);
	minLineSegmentLength = geometry["min_line_segment_length"].as<double>(10.0);
	maxLineSegmentOffset = geometry["max_line_segment_offset"].as<double>(10.0);
	maxLineSegmentAngle = geometry["max_line_segment_angle"].as<double>(3.0) * M_PI/180.0;

	YAML::Node color = getOptional(config["color"]);
	referenceForce = color["reference_force"].as<float>(0.1f);
	historyForce = color["history_force"].as<float>(0.7f);
	orangeReference = color["orange"].as<Eigen::Vector3i>(Eigen::Vector3i{192, 128, 64});
	fieldReference = color["field"].as<Eigen::Vector3i>(Eigen::Vector3i{128, 128, 128});
	yellowReference = color["yellow"].as<Eigen::Vector3i>(Eigen::Vector3i{255, 128, 0});
	blueReference = color["blue"].as<Eigen::Vector3i>(Eigen::Vector3i{0, 128, 255});
	greenReference = color["green"].as<Eigen::Vector3i>(Eigen::Vector3i{0, 255, 128});
	pinkReference = color["pink"].as<Eigen::Vector3i>(Eigen::Vector3i{255, 0, 128});
	orange = orangeReference;
	field = fieldReference;
	yellow = yellowReference;
	blue = blueReference;
	green = greenReference;
	pink = pinkReference;

	YAML::Node debug = getOptional(config["debug"]);
	groundTruth = debug["ground_truth"].as<std::string>("gt.yml");
	bool waitForGeometry = debug["wait_for_geometry"].as<bool>(false);
	debugImages = debug["debug_images"].as<bool>(false);

	YAML::Node network = getOptional(config["network"]);
	gcSocket = std::make_shared<GCSocket>(network["gc_ip"].as<std::string>("224.5.23.1"), network["gc_port"].as<int>(10003), YAML::LoadFile(config["bot_heights_file"].as<std::string>("robot-heights.yml")).as<std::map<std::string, double>>());
	socket = std::make_shared<VisionSocket>(network["vision_ip"].as<std::string>("224.5.23.2"), network["vision_port"].as<int>(10006), camId, gcSocket->defaultBotHeight);
	perspective = std::make_shared<Perspective>(socket, camId);

	YAML::Node stream = getOptional(config["stream"]);
	rtpStreamer = std::make_shared<RTPStreamer>(stream["active"].as<bool>(true), "rtp://" + stream["ip_base_prefix"].as<std::string>("224.5.23.") + std::to_string(stream["ip_base_end"].as<int>(100) + camId) + ":" + std::to_string(stream["port"].as<int>(10100)));
	rawFeed = stream["raw_feed"].as<bool>(false);

	raw2quadKernel = openCl->compile(kernel_raw2quad_cl, camera->format().kernelOptions);
	resampling = openCl->compile(kernel_resampling_cl, camera->format().kernelOptions);
	gradientDot = openCl->compile(kernel_gradientDot_cl);
	satHorizontal = openCl->compile(kernel_satHorizontal_cl);
	satVertical = openCl->compile(kernel_satVertical_cl);
	satBlobCenter = openCl->compile(kernel_satBlobCenter_cl);
	quad2rgbaKernel = openCl->compile(kernel_quad2rgba_cl, camera->format().kernelOptions);
	quad2nv12 = openCl->compile(kernel_quad2nv12_cl, camera->format().kernelOptions);
	rgba2nv12 = openCl->compile(kernel_rgba2nv12_cl);
	f2nv12 = openCl->compile(kernel_f2nv12_cl);

	while(waitForGeometry && !socket->getGeometryVersion()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		socket->geometryCheck();
	}
}

void Resources::raw2quad(const RawImage& img, std::shared_ptr<CLImage>* channels) {
	for(int i = 0; i < 4; i++)
		channels[i] = openCl->acquire(&PixelFormat::U8, img.width, img.height, img.name);

	OpenCL::await(raw2quadKernel, cl::EnqueueArgs(cl::NDRange(img.width, img.height)), img.buffer, channels[0]->image, channels[1]->image, channels[2]->image, channels[3]->image);
}

std::shared_ptr<CLImage> Resources::quad2rgba(std::shared_ptr<CLImage>* channels) {
	std::shared_ptr<CLImage> rgba = openCl->acquire(&PixelFormat::RGBA8, channels[0]->width, channels[0]->height, channels[0]->name);
	OpenCL::await(quad2rgbaKernel, cl::EnqueueArgs(cl::NDRange(channels[0]->width, channels[0]->height)), channels[0]->image, channels[1]->image, channels[2]->image, channels[3]->image, rgba->image);
	return rgba;
}

void Resources::rgba2blobCenter(const std::shared_ptr<CLImage>* channels, std::shared_ptr<CLImage>& flat, std::shared_ptr<CLImage>& gradDot, std::shared_ptr<CLImage>& blobCenter) {
	cl::NDRange visibleFieldRange(perspective->reprojectedFieldSize[0], perspective->reprojectedFieldSize[1]);
	flat = openCl->acquire(&PixelFormat::RGBA8, perspective->reprojectedFieldSize[0], perspective->reprojectedFieldSize[1], channels[0]->name);
	gradDot = openCl->acquire(&PixelFormat::F32, perspective->reprojectedFieldSize[0], perspective->reprojectedFieldSize[1], channels[0]->name);
	std::shared_ptr<CLImage> gradDotHor = openCl->acquire(&PixelFormat::F32, perspective->reprojectedFieldSize[0], perspective->reprojectedFieldSize[1], channels[0]->name);
	std::shared_ptr<CLImage> gradDotSat = openCl->acquire(&PixelFormat::F32, perspective->reprojectedFieldSize[0], perspective->reprojectedFieldSize[1], channels[0]->name);
	blobCenter = openCl->acquire(&PixelFormat::F32, perspective->reprojectedFieldSize[0], perspective->reprojectedFieldSize[1], channels[0]->name);

	cl::Event e1 = OpenCL::run(resampling, cl::EnqueueArgs(visibleFieldRange), channels[0]->image, channels[1]->image, channels[2]->image, channels[3]->image, flat->image, perspective->getCLCameraModel(), (float)gcSocket->maxBotHeight, perspective->fieldScale, perspective->visibleFieldExtent[0], perspective->visibleFieldExtent[2]);
	cl::Event e2 = OpenCL::run(gradientDot, cl::EnqueueArgs(e1, visibleFieldRange), flat->image, gradDot->image, (int)ceilf(perspective->maxBlobRadius / perspective->fieldScale) / 3);
	cl::Event e3 = OpenCL::run(satHorizontal, cl::EnqueueArgs(e2, cl::NDRange(perspective->reprojectedFieldSize[1])), gradDot->image, gradDotHor->image);
	cl::Event e4 = OpenCL::run(satVertical, cl::EnqueueArgs(e3, cl::NDRange(perspective->reprojectedFieldSize[0])), gradDotHor->image, gradDotSat->image);
	OpenCL::await(satBlobCenter, cl::EnqueueArgs(e4, visibleFieldRange), gradDotSat->image, blobCenter->image, (int)ceilf(perspective->minBlobRadius / perspective->fieldScale));
}

void Resources::streamQuad(std::shared_ptr<CLImage>* channels) {
	std::shared_ptr<RawImage> nv12 = openCl->acquireNV12(channels[0]->width, channels[0]->height);
	OpenCL::await(quad2nv12, cl::EnqueueArgs(cl::NDRange(channels[0]->width, channels[0]->height)), channels[0]->image, channels[1]->image, channels[2]->image, channels[3]->image, nv12->buffer);
	rtpStreamer->sendFrame(nv12);
}

void Resources::streamImage(CLImage &img) {
	cl::Kernel kernel;
	if(img.format == &PixelFormat::RGBA8) {
		kernel = rgba2nv12;
	} else if(img.format == &PixelFormat::F32) {
		kernel = f2nv12;
	} else {
		std::cerr << "[Resources] Unimplemented pixel format submitted for streaming." << std::endl;
		return;
	}

	std::shared_ptr<RawImage> nv12 = openCl->acquireNV12(img.width, img.height);
	OpenCL::await(kernel, cl::EnqueueArgs(cl::NDRange(img.width, img.height)), img.image, nv12->buffer);
	rtpStreamer->sendFrame(nv12);
}