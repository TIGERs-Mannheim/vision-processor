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
#include <eigen3/Eigen/Geometry>

#include "proto/ssl_vision_geometry.pb.h"

float goalBoundaryWidth(const SSL_GeometryFieldSize& field);

void visibleFieldExtentEstimation(int camId, int camAmount, const SSL_GeometryFieldSize& field, bool withBoundary, Eigen::Vector2f& min, Eigen::Vector2f& max);


class CameraModel {
public:
	CameraModel();
	CameraModel(const Eigen::Vector2i& size, int camId, int camAmount, float cameraHeight, const SSL_GeometryFieldSize& field);

	explicit CameraModel(const SSL_GeometryCameraCalibration& calib);
	[[nodiscard]] SSL_GeometryCameraCalibration getProto(int camId) const;

	void updateDerived();
	void ensureSize(const Eigen::Vector2i& newSize);

	[[nodiscard]] Eigen::Vector2f normalizeUndistort(const Eigen::Vector2f& p) const;
	[[nodiscard]] Eigen::Vector2f undistort(const Eigen::Vector2f& p) const;

	[[nodiscard]] Eigen::Vector2f field2image(const Eigen::Vector3f& p) const;
	[[nodiscard]] Eigen::Vector3f image2field(const Eigen::Vector2f& p, float height) const;

	void updateEuler(const Eigen::Vector3f& euler);
	Eigen::Vector3f getEuler();

	float focalLength;
	Eigen::Vector2f principalPoint;
	float distortionK2 = 0.0f;
	Eigen::Vector3f pos = Eigen::Vector3f(0.0f, 0.0f, 5000.0f); // camera position in field coordinate system
	Eigen::Quaternionf f2iOrientation = Eigen::Quaternionf(0.0f, -1.0f, 0.0f, 0.0f); // orientation from field to image plane
	Eigen::Vector2i size;

	Eigen::Affine3f f2iTransformation;
	Eigen::Matrix3f i2fOrientation;
};

std::ostream& operator<<(std::ostream &out, const CameraModel& data);
