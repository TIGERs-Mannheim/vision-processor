#include "Perspective.h"

#include <cmath>

static void updateExtent(Eigen::Vector4f& visibleFieldExtent, const Eigen::Vector3f& point) {
	if(point.x() < visibleFieldExtent[0])
		visibleFieldExtent[0] = point.x();
	if(point.x() > visibleFieldExtent[1])
		visibleFieldExtent[1] = point.x();

	if(point.y() < visibleFieldExtent[2])
		visibleFieldExtent[2] = point.y();
	if(point.y() > visibleFieldExtent[3])
		visibleFieldExtent[3] = point.y();
}

void Perspective::geometryCheck(const int width, const int height, const double maxBotHeight) {
	Eigen::Vector2i size(width, height);
	if(socket->getGeometryVersion() == geometryVersion && model.size == size)
		return;

	bool calibFound = false;
	for(const SSL_GeometryCameraCalibration& calib : socket->getGeometry().calib()) {
		if(calib.camera_id() == camId) {
			calibFound = true;
			model = CameraModel(calib);
			break;
		}
	}

	if(!calibFound)
		return;

	model.ensureSize(size);
	geometryVersion = socket->getGeometryVersion();
	field = socket->getGeometry().field();

	//update visibleFieldExtent
	Eigen::Vector2f center = model.image2field({0.0f, 0.0f}, (float)maxBotHeight).head<2>();
	visibleFieldExtent = {center.x(), center.x(), center.y(), center.y()};

	for(int x = 0; x < width; x++)
		updateExtent(visibleFieldExtent, model.image2field({(float)x, 0.0f}, (float)maxBotHeight));
	for(int x = 0; x < width; x++)
		updateExtent(visibleFieldExtent, model.image2field({(float)x, (float)height - 1.0f}, (float)maxBotHeight));

	for(int y = 0; y < height; y++)
		updateExtent(visibleFieldExtent, model.image2field({0.0f, (float)y}, (float)maxBotHeight));
	for(int y = 0; y < height; y++)
		updateExtent(visibleFieldExtent, model.image2field({(float)width - 1.0f, (float)y}, (float)maxBotHeight));

	Eigen::Vector2f unclampedFieldSize = Eigen::Vector2f(visibleFieldExtent[1] - visibleFieldExtent[0], visibleFieldExtent[3] - visibleFieldExtent[2]);
	fieldScale = std::min(unclampedFieldSize.maxCoeff() / (float)size.maxCoeff(), unclampedFieldSize.minCoeff() / (float)size.minCoeff());

	// clamp to field boundaries
	const float halfLength = (float)field.field_length()/2.0f + (float)field.boundary_width();
	const float halfWidth = (float)field.field_width()/2.0f + (float)field.boundary_width();
	visibleFieldExtent[0] = std::max(visibleFieldExtent[0], -halfLength);
	visibleFieldExtent[1] = std::min(visibleFieldExtent[1], halfLength);
	visibleFieldExtent[2] = std::max(visibleFieldExtent[2], -halfWidth);
	visibleFieldExtent[3] = std::min(visibleFieldExtent[3], halfWidth);

	Eigen::Vector2f fieldSize = Eigen::Vector2f(visibleFieldExtent[1] - visibleFieldExtent[0], visibleFieldExtent[3] - visibleFieldExtent[2]);
	reprojectedFieldSize = (fieldSize * (1.0f / fieldScale)).array().rint().cast<int>();

	std::cout << "[Perspective] Visible field extent: " << visibleFieldExtent.transpose() << "mm (xmin,xmax,ymin,ymax) Field scale: " << fieldScale << "mm/px" << std::endl;
}

V2 Perspective::image2field(V2 pos, double height) const {
	Eigen::Vector3f p = model.image2field({pos.x, pos.y}, (float)height);
	return {p.x(), p.y()};
}

V2 Perspective::field2image(V3 pos) const {
	Eigen::Vector2f p = model.field2image({(float)pos.x, (float)pos.y, (float)pos.z});
	return {p.x(), p.y()};
}

Eigen::Vector2f Perspective::flat2field(const Eigen::Vector2f& pos) const {
	return pos * fieldScale + Eigen::Vector2f(visibleFieldExtent[0], visibleFieldExtent[2]);
}

Eigen::Vector2f Perspective::field2flat(const Eigen::Vector2f& pos) const {
	return (pos - Eigen::Vector2f(visibleFieldExtent[0], visibleFieldExtent[2])) / fieldScale;
}


ClPerspective Perspective::getClPerspective() const {
	const Eigen::Matrix3f& i2f = model.i2fOrientation;
	const Eigen::Matrix3f& f2i = model.f2iOrientation.toRotationMatrix();
	return {
			{model.size.x(), model.size.y()},
			1/model.focalLength,
			{model.principalPoint.x(), model.principalPoint.y()},
			model.distortionK2,
			{
				i2f(0, 0), i2f(0, 1), i2f(0, 2),
				i2f(1, 0), i2f(1, 1), i2f(1, 2),
				i2f(2, 0), i2f(2, 1), i2f(2, 2)
			},
			{model.pos.x(), model.pos.y(), model.pos.z()},
			{(field.field_length() + 2*field.boundary_width())/10, (field.field_width() + 2*field.boundary_width())/10},
			model.focalLength,
			{
				f2i(0, 0), f2i(0, 1), f2i(0, 2),
				f2i(1, 0), f2i(1, 1), f2i(1, 2),
				f2i(2, 0), f2i(2, 1), f2i(2, 2)
			}
	};
}