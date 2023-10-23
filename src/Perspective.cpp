#include "Perspective.h"

static inline V3 rotation(V3 v, V3 x, V3 y, V3 z) {
	return {
		x.x * v.x + y.x * v.y + z.x * v.z,
		x.y * v.x + y.y * v.y + z.y * v.z,
		x.z * v.x + y.z * v.y + z.z * v.z
	};
}


void Perspective::geometryCheck() {
	if(socket->getGeometryVersion() == geometryVersion)
		return;

	geometryVersion = socket->getGeometryVersion();
	calib = socket->getGeometry().calib(camId); //TODO
	field = socket->getGeometry().field();

	orientation = { -calib.q0(), -calib.q1(), -calib.q2(), calib.q3() };
	double qLength = sqrt(orientation.q0*orientation.q0 + orientation.q1*orientation.q1 + orientation.q2*orientation.q2 + orientation.q3*orientation.q3);
	orientation.q0 /= qLength; orientation.q1 /= qLength; orientation.q2 /= qLength; orientation.q3 /= qLength;
	double x2 = orientation.q0*orientation.q0;
	double y2 = orientation.q1*orientation.q1;
	double z2 = orientation.q2*orientation.q2;
	double xy = orientation.q0*orientation.q1;
	double xz = orientation.q0*orientation.q2;
	double yz = orientation.q1*orientation.q2;
	double wx = orientation.q3*orientation.q0;
	double wy = orientation.q3*orientation.q1;
	double wz = orientation.q3*orientation.q2;
	rX = {1.0 - 2.0 * (y2 + z2), 2.0 * (xy + wz), 2.0 * (xz - wy)};
	rY = {2.0 * (xy - wz), 1.0 - 2.0 * (x2 + z2), 2.0 * (yz + wx)};
	rZ = {2.0 * (xz + wy), 2.0 * (yz - wx), 1.0 - 2.0 * (x2 + y2)};

	cameraPos = rotation({calib.tx(), calib.ty(), calib.tz()}, rX, rY, rZ);
}

V2 Perspective::image2field(V2 pos, double height) {
	V2 normalized = {
			(pos.x - calib.principal_point_x()/2) / (calib.focal_length()/2),
			(pos.y - calib.principal_point_y()/2) / (calib.focal_length()/2)
	};

	double distortion = 1.0 + (normalized.x*normalized.x + normalized.y*normalized.y) * calib.distortion();

	V3 camRay = rotation({distortion*normalized.x, distortion*normalized.y, 1.0}, rX, rY, rZ);

	if(camRay.z >= 0) // Over horizon
		return { NAN, NAN };

	double zFactor = (cameraPos.z + height) / camRay.z;
	V2 worldRay = {
			camRay.x*zFactor - cameraPos.x,
			camRay.y*zFactor - cameraPos.y
	};

	return worldRay;
}

int Perspective::getGeometryVersion() {
	return geometryVersion;
}

int Perspective::getWidth() {
	return calib.pixel_image_width()/2;
}

int Perspective::getHeight() {
	return calib.pixel_image_height()/2;
}

int Perspective::getFieldLength() {
	return field.field_length();
}

int Perspective::getFieldWidth() {
	return field.field_width();
}

int Perspective::getBoundaryWidth() {
	return field.boundary_width();
}

ClPerspective Perspective::getClPerspective() {
	return {
			{(int)calib.pixel_image_width(), (int)calib.pixel_image_height()},
			2/calib.focal_length(),
			{calib.principal_point_x()/2, calib.principal_point_y()/2},
			calib.distortion(),
			{
				(float)rX.x, (float)rY.x, (float)rZ.x,
				(float)rX.y, (float)rY.y, (float)rZ.y,
				(float)rX.z, (float)rY.z, (float)rZ.z,
			},
			{(float)cameraPos.x, (float)cameraPos.y, (float)cameraPos.z}
	};
}
