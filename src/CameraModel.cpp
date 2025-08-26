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
#include "CameraModel.h"


void visibleFieldExtentEstimation(const int camId, const int camAmount, const SSL_GeometryFieldSize& field, const bool withBoundary, Eigen::Vector2f &min, Eigen::Vector2f &max) {
	Eigen::Vector2f fieldSize(field.field_length(), field.field_width());

	Eigen::Vector2i size;
	size.setOnes();
	for(int i = camAmount; i > 1; i /= 2) {
		if(fieldSize[0]/size[0] >= fieldSize[1]/size[1])
			size[0] *= 2;
		else
			size[1] *= 2;
	}

	Eigen::Vector2i pos;
	pos.setZero();
	for(int i = camId % camAmount; i > 0; i--) {
		pos[1]++;
		if(pos[1] == size[1]) {
			pos[1] = 0;
			pos[0]++;
		}
	}

	Eigen::Vector2f extentSize = fieldSize.array() / size.cast<float>().array();
	min = extentSize.array()*pos.cast<float>().array() - fieldSize.array()/2;
	max = min + extentSize;

	if(withBoundary) {
		if(pos[0] == 0)
			min[0] -= (float)field.boundary_width();
		if(pos[1] == 0)
			min[1] -= (float)field.boundary_width();
		if(pos[0] == size[0]-1)
			max[0] += (float)field.boundary_width();
		if(pos[1] == size[1]-1)
			max[1] += (float)field.boundary_width();
	}
}


CameraModel::CameraModel(): focalLength(1224.0f), principalPoint({612, 512}), size({1224, 1024}) {
	updateDerived();
}

CameraModel::CameraModel(const Eigen::Vector2i &size, int camId, int camAmount, const float cameraHeight, const SSL_GeometryFieldSize& field):
		principalPoint(size.cast<float>()/2),
		size(size) {
	Eigen::Vector2f min;
	Eigen::Vector2f max;
	visibleFieldExtentEstimation(camId, camAmount, field, true, min, max);
	pos.head<2>() = min/2 + max/2;
	if(cameraHeight != 0.f)
		pos.z() = cameraHeight;

	//Whole field visible
	Eigen::Vector2f orderedSize(size.maxCoeff(), size.minCoeff());
	Eigen::Vector2f orderedExtent((max - min).maxCoeff(), (max - min).minCoeff());
	focalLength = ((orderedSize - principalPoint).array() * pos.z() / orderedExtent.array()).minCoeff() * 2;

	updateDerived();
}

CameraModel::CameraModel(const SSL_GeometryCameraCalibration& calib):
		focalLength(calib.focal_length()),
		principalPoint(calib.principal_point_x(), calib.principal_point_y()),
		distortionK2(calib.distortion()),
		f2iOrientation(calib.q3(), calib.q0(), calib.q1(), calib.q2()),
		size(calib.pixel_image_width(), calib.pixel_image_height()) {
	pos = f2iOrientation.inverse() * -Eigen::Vector3f(calib.tx(), calib.ty(), calib.tz());
	updateDerived();
}

SSL_GeometryCameraCalibration CameraModel::getProto(int camId) const {
	SSL_GeometryCameraCalibration proto;
	proto.set_camera_id(camId);
	proto.set_focal_length(focalLength);
	proto.set_principal_point_x(principalPoint.x());
	proto.set_principal_point_y(principalPoint.y());
	proto.set_distortion(distortionK2);
	proto.set_q0(f2iOrientation.x());
	proto.set_q1(f2iOrientation.y());
	proto.set_q2(f2iOrientation.z());
	proto.set_q3(f2iOrientation.w());
	Eigen::Vector3f iPos = f2iOrientation * -pos;
	proto.set_tx(iPos.x());
	proto.set_ty(iPos.y());
	proto.set_tz(iPos.z());
	proto.set_derived_camera_world_tx(pos.x());
	proto.set_derived_camera_world_ty(pos.y());
	proto.set_derived_camera_world_tz(pos.z());
	proto.set_pixel_image_width(size.x());
	proto.set_pixel_image_height(size.y());
	return proto;
}

void CameraModel::updateDerived() {
	f2iOrientation.normalize();
	i2fOrientation = f2iOrientation.inverse();
	f2iTransformation = Eigen::Affine3f(f2iOrientation) * Eigen::Translation3f(-pos);
}

void CameraModel::ensureSize(const Eigen::Vector2i& newSize) {
	if(size == newSize)
		return;

	const float factor = (float)newSize.x()/(float)size.x();
	if((float)size.y()*factor != (float)newSize.y())
		std::cerr << "[CameraModel] ensureSize with diverging aspect ratios" << std::endl;

	size = newSize;
	focalLength *= factor;
	principalPoint *= factor;
}

Eigen::Vector2f CameraModel::normalizeUndistort(const Eigen::Vector2f& p) const {
	Eigen::Vector2f normalized = (p - principalPoint) / focalLength;
	normalized *= 1.0f + distortionK2 * normalized.dot(normalized);
	return normalized;
}

Eigen::Vector2f CameraModel::undistort(const Eigen::Vector2f &p) const {
	return normalizeUndistort(p) * focalLength + principalPoint;
}

Eigen::Vector2f CameraModel::field2image(const Eigen::Vector3f& p) const {
	Eigen::Vector3f camRay = f2iTransformation * p;
	Eigen::Vector2f normalized = camRay.head<2>() / camRay.z();

	//Apply distortion
	Eigen::Vector2f original = normalized;
	for(int i = 0; i < 10; i++)
		normalized = original / (1 + distortionK2 * normalized.dot(normalized));

	return focalLength * normalized + principalPoint;
}

Eigen::Vector3f CameraModel::image2field(const Eigen::Vector2f& p, const float height) const {
	const Eigen::Vector2f normalized = normalizeUndistort(p);
	Eigen::Vector3f camRay(normalized.x(), normalized.y(), 1.0f);
	camRay = i2fOrientation * camRay;

	if(camRay.z() >= 0) {
		//std::cerr << "[CameraModel] Transformation over horizon: " << p.transpose() << std::endl;
		return { NAN, NAN, NAN };
	}

	camRay = camRay*((-pos.z() + height) / camRay.z()) + pos;
	camRay.z() = height;
	return camRay;
}

void CameraModel::updateEuler(const Eigen::Vector3f &euler) {
	f2iOrientation = Eigen::AngleAxisf(euler.x(), Eigen::Vector3f::UnitX()) * Eigen::AngleAxisf(euler.y(), Eigen::Vector3f::UnitY()) * Eigen::AngleAxisf(euler.z(), Eigen::Vector3f::UnitZ());
	updateDerived();
}

Eigen::Vector3f CameraModel::getEuler() {
	return f2iOrientation.toRotationMatrix().eulerAngles(0, 1, 2);
}


std::ostream& operator<<(std::ostream& out, const CameraModel& data) {
	out << data.getProto(0).ShortDebugString();
	return out;
}
