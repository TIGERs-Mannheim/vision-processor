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
#include "Perspective.h"
#include "pattern.h"
#include "proto/ssl_vision_wrapper.pb.h"

#include <cmath>

static void updateExtent(Eigen::Vector4f& visibleFieldExtent, const Eigen::Vector2f& point) {
	if(point.x() < visibleFieldExtent[0])
		visibleFieldExtent[0] = point.x();
	if(point.x() > visibleFieldExtent[1])
		visibleFieldExtent[1] = point.x();

	if(point.y() < visibleFieldExtent[2])
		visibleFieldExtent[2] = point.y();
	if(point.y() > visibleFieldExtent[3])
		visibleFieldExtent[3] = point.y();
}

void Perspective::geometryCheck(const int width, const int height, const double maxBotHeight, const float resamplingFactor) {
	Eigen::Vector2i size(width, height);
	if(socket->getGeometryVersion() == geometryVersion && model.size == size)
		return;

	bool calibFound = false;
	for(const SSL_GeometryCameraCalibration& calib : socket->getGeometry().calib()) {
		if(calib.camera_id() == camId) {
			calibFound = true;
			model = CameraModel(calib);
			if(!calib.has_derived_camera_world_tx() || !calib.has_derived_camera_world_ty() || !calib.has_derived_camera_world_tz()) {
				socket->send(model.getProto(camId));
				SSL_WrapperPacket wrapper;
				wrapper.mutable_geometry()->CopyFrom(socket->getGeometry());
				wrapper.mutable_geometry()->clear_calib();
				wrapper.mutable_geometry()->add_calib()->CopyFrom(model.getProto(camId));
				socket->send(wrapper);
			}
			break;
		}
	}

	if(!calibFound) {
		if(socket->getGeometry().calib_size() == 0) // Don't recalibrate when its just another vision_processor instance sending it's camera calibration
			geometryVersion = 0; // Camera calibration has been cleared, recalibrate

		return;
	}

	model.ensureSize(size);
	geometryVersion = socket->getGeometryVersion();
	field = socket->getGeometry().field();

	minBlobRadius = std::min({CENTER_BLOB_RADIUS, SIDE_BLOB_RADIUS, field.ball_radius()});
	maxBlobRadius = std::max({CENTER_BLOB_RADIUS, SIDE_BLOB_RADIUS, field.ball_radius()});

	//calculate optimal fieldScale
	float minFieldScale = MAXFLOAT;
	float maxFieldScale = 0;
	float fieldScaleSum = 0;
	int n = 0;
	for(int y = 0; y < height-1; y++) {
		for(int x = 0; x < width-1; x++) {
			Eigen::Vector2f pos = model.image2field({x, y}, (float)maxBotHeight).head<2>();
			if(abs(pos.x()) < (float)field.field_length()/2.f + goalBoundaryWidth(field) && abs(pos.y()) < (float)field.field_width()/2.f + (float)field.boundary_width()) {
				float dx = (model.image2field({x+1, y}, (float)maxBotHeight).head<2>() - pos).norm();
				float dy = (model.image2field({x, y+1}, (float)maxBotHeight).head<2>() - pos).norm();

				minFieldScale = std::min(minFieldScale, std::min(dx, dy));
				maxFieldScale = std::max(maxFieldScale, std::max(dx, dy));
				fieldScaleSum += dx + dy;
				n += 2;
			}
		}
	}
	fieldScale = fieldScaleSum / (float)n * resamplingFactor;
	std::cout << "[Perspective] Field scale: " << minFieldScale << "mm/px < " << fieldScale << "mm/px < " << maxFieldScale << "mm/px" << std::endl;

	//update visibleFieldExtent
	Eigen::Vector2f center = model.image2field({0.0f, 0.0f}, (float)maxBotHeight).head<2>();
	visibleFieldExtent = {center.x(), center.x(), center.y(), center.y()};

	for(int x = 0; x < width; x++) {
		updateExtent(visibleFieldExtent, model.image2field({(float)x, 0.0f}, (float)maxBotHeight).head<2>());
		updateExtent(visibleFieldExtent, model.image2field({(float)x, (float)height - 1.0f}, (float)maxBotHeight).head<2>());
	}
	for(int y = 0; y < height; y++) {
		updateExtent(visibleFieldExtent, model.image2field({0.0f, (float)y}, (float)maxBotHeight).head<2>());
		updateExtent(visibleFieldExtent, model.image2field({(float)width - 1.0f, (float)y}, (float)maxBotHeight).head<2>());
	}

	// clamp to field boundaries
	const float halfLength = (float)field.field_length()/2.0f + goalBoundaryWidth(field);
	const float halfWidth = (float)field.field_width()/2.0f + (float)field.boundary_width();
	visibleFieldExtent[0] = std::max(visibleFieldExtent[0], -halfLength);
	visibleFieldExtent[1] = std::min(visibleFieldExtent[1], halfLength);
	visibleFieldExtent[2] = std::max(visibleFieldExtent[2], -halfWidth);
	visibleFieldExtent[3] = std::min(visibleFieldExtent[3], halfWidth);

	Eigen::Vector2f fieldSize = Eigen::Vector2f(visibleFieldExtent[1] - visibleFieldExtent[0], visibleFieldExtent[3] - visibleFieldExtent[2]);
	reprojectedFieldSize = (fieldSize / fieldScale).array().rint().cast<int>();

	//Make size even for rtpstreamer
	if(reprojectedFieldSize[0] % 2)
		reprojectedFieldSize[0]++;
	if(reprojectedFieldSize[1] % 2)
		reprojectedFieldSize[1]++;

	std::cout << "[Perspective] Visible field extent: " << visibleFieldExtent.transpose() << "mm (xmin,xmax,ymin,ymax) Field scale: " << fieldScale << "mm/px" << std::endl;
}

Eigen::Vector2f Perspective::flat2field(const Eigen::Vector2f& pos) const {
	return pos * fieldScale + Eigen::Vector2f(visibleFieldExtent[0], visibleFieldExtent[2]);
}

Eigen::Vector2f Perspective::field2flat(const Eigen::Vector2f& pos) const {
	return (pos - Eigen::Vector2f(visibleFieldExtent[0], visibleFieldExtent[2])) / fieldScale;
}


CLCameraModel Perspective::getCLCameraModel() const {
	const Eigen::Matrix3f& f2i = model.f2iOrientation.toRotationMatrix();
	return {
			{model.size.x(), model.size.y()},
			model.focalLength,
			{model.principalPoint.x(), model.principalPoint.y()},
			model.distortionK2,
			{
					f2i(0, 0), f2i(0, 1), f2i(0, 2),
					f2i(1, 0), f2i(1, 1), f2i(1, 2),
					f2i(2, 0), f2i(2, 1), f2i(2, 2)
			},
			{model.pos.x(), model.pos.y(), model.pos.z()}
	};
}
