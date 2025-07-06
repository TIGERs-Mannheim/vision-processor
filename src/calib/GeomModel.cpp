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
#include <opencv2/opencv.hpp>
#include "GeomModel.h"
#include "Distortion.h"
#include "LineDetection.h"
#include "proto/ssl_vision_wrapper.pb.h"

#include <eigen3/unsupported/Eigen/LevenbergMarquardt>


float dist(const cv::Vec2f& v1, const cv::Vec2f& v2) {
	cv::Vec2f d = v2-v1;
	return sqrtf(d.dot(d));
}

void visibleFieldExtent(const Resources &r, const bool withBoundary, Eigen::Vector2f &min, Eigen::Vector2f &max) {
	return visibleFieldExtentEstimation(r.camId, r.cameraAmount, r.socket->getGeometry().field(), withBoundary, min, max);
}

Eigen::Vector2f cv2eigen(const cv::Vec2f& v) {
	return {v[0], v[1]};
}

typedef struct LineArc {
	Eigen::Vector2f center;
	float radius;
	float a1, a2;
} LineArc;

static void fieldToLines(const Resources& r, std::vector<std::pair<Eigen::Vector2f, Eigen::Vector2f>>& lines, std::vector<LineArc>& arcs) {
	const SSL_GeometryFieldSize& field = r.socket->getGeometry().field();

	for(const SSL_FieldLineSegment& line : field.field_lines())
		lines.emplace_back(Eigen::Vector2f(line.p1().x(), line.p1().y()), Eigen::Vector2f(line.p2().x(), line.p2().y()));

	for(const SSL_FieldCircularArc& arc : field.field_arcs()) {
		arcs.push_back({
							   .center = {arc.center().x(), arc.center().y()},
							   .radius = arc.radius(),
							   .a1 = arc.a1(),
							   .a2 = arc.a2()
					   });
	}
}

static float sqPointLineSegmentDistance(const std::pair<Eigen::Vector2f, Eigen::Vector2f>& line, const Eigen::Vector2f& point) {
	//Adapted from Grumdrig https://stackoverflow.com/a/1501725 CC BY-SA 4.0
	const Eigen::Vector2f v = line.second - line.first;
	const Eigen::Vector2f w = point - line.first;
	const float t = std::max(0.0f, std::min(1.0f, w.dot(v) / v.dot(v)));
	const Eigen::Vector2f delta = w - t * v;
	return delta.dot(delta);
}

static float minPointModelDistance(const std::vector<std::pair<Eigen::Vector2f, Eigen::Vector2f>>& lines, const std::vector<LineArc>& arcs, const Eigen::Vector2f& fieldPixel) {
	float distance = MAXFLOAT;
	for(const auto& line : lines) {
		distance = std::min(distance, sqPointLineSegmentDistance(line, fieldPixel));
	}

	distance = sqrtf(distance);

	for(const auto& arc : arcs) {
		const Eigen::Vector2f pixel2center = fieldPixel - arc.center;
		/*float angle = atan2f(pixel2center.y(), pixel2center.x());
		if(angle < 0)
			angle += 2*M_PI;*/

		//TODO cases outside of angle >= arc.a1 && angle <= arc.a2
		distance = std::min(distance, abs(sqrtf(pixel2center.dot(pixel2center)) - arc.radius));
	}

	return distance;
}

struct DirectGeometryFit : public Eigen::DenseFunctor<float> {
	const std::vector<Eigen::Vector2f>& linePixels;
	const std::vector<std::vector<Eigen::Vector2f>>& mergedPixels;
	const CameraModel& reference;
	const bool calibHeight;

	std::vector<std::pair<Eigen::Vector2f, Eigen::Vector2f>> lines;
	std::vector<LineArc> arcs;
	std::vector<Eigen::Vector2f> modelPoints;

	explicit DirectGeometryFit(const Resources& r, const std::vector<Eigen::Vector2f>& linePixels, const std::vector<std::vector<Eigen::Vector2f>>& mergedPixels, const CameraModel& model, const bool calibHeight): linePixels(linePixels), mergedPixels(mergedPixels), reference(model), calibHeight(calibHeight) {
		fieldToLines(r, lines, arcs);

		float stepSize = 100.f;
		for(const std::pair<Eigen::Vector2f, Eigen::Vector2f>& line : lines) {
			Eigen::Vector2f delta = line.second - line.first;
			int steps = (int)(delta.norm() / stepSize);
			delta /= (float)steps;
			for(int i = 0; i < steps; i++)
				modelPoints.emplace_back(line.first + delta*i);
		}

		for(const LineArc& arc : arcs) {
			float step = 2.f * asinf((stepSize/2.f) / arc.radius);
			for(float i = arc.a1; i <= arc.a2; i += step)
				modelPoints.emplace_back(arc.center + Eigen::Vector2f(cosf(i), sinf(i))*arc.radius);
		}

		Eigen::Vector2f extentMin;
		Eigen::Vector2f extentMax;
		visibleFieldExtent(r, true, extentMin, extentMax);
		modelPoints.erase(std::remove_if(modelPoints.begin(), modelPoints.end(), [&](const auto& item) {
			return item.x() < extentMin.x() || item.x() > extentMax.x() || item.y() < extentMin.y() || item.y() > extentMax.y();
		}), modelPoints.end());
	}

	int operator()(const InputType &x, ValueType& fvec) const {
		CameraModel model = reference;
		model.focalLength = x[0];
		model.updateEuler({x[1], x[2], x[3]});
		model.pos.x() = x[4];
		model.pos.y() = x[5];
		if(calibHeight)
			model.pos.z() = x[6];
		calibrateDistortion(mergedPixels, model);
		model.updateDerived();

		int i = 0;
		for(const Eigen::Vector2f& point : modelPoints) {
			Eigen::Vector2f image = model.field2image({point.x(), point.y(), 0.f});

			float min = MAXFLOAT;
			for(const Eigen::Vector2f& pixel : linePixels) {
				float sqNorm = (pixel - image).squaredNorm();
				if(sqNorm < min)
					min = sqNorm;
			}

			fvec[i++] = min;
		}

		/*for(const Eigen::Vector2f& pixel : linePixels) {
			Eigen::Vector2f field = model.image2field(pixel, 0.0f).head<2>();
			fvec[i++] = minPointModelDistance(lines, arcs, field);
			//fvec[i++] = sqrtf(minPointModelDistance(lines, arcs, field)); //sqrtf-model
		}*/

		return 0;
	}

	int values() const {
		return modelPoints.size();
		//return linePixels.size();
	}
};

static bool pointAtLine(const CameraModel& model, const std::vector<std::pair<Eigen::Vector2f, Eigen::Vector2f>>& lines, const std::vector<LineArc>& arcs, const float halfLineWidth, const Eigen::Vector2f& linePixel) {
	const float sqHalfLineWidth = halfLineWidth*halfLineWidth;
	const Eigen::Vector2f fieldPixel = model.image2field(linePixel, 0.0f).head<2>();
	for(const auto& line : lines) {
		if(sqPointLineSegmentDistance(line, fieldPixel) <= sqHalfLineWidth)
			return true;
	}

	for(const auto& arc : arcs) {
		const Eigen::Vector2f pixel2center = fieldPixel - arc.center;
		float angle = atan2f(pixel2center.y(), pixel2center.x());
		if(angle < 0)
			angle += 2*M_PI;

		if(abs(sqrtf(pixel2center.dot(pixel2center)) - arc.radius) <= halfLineWidth && angle >= arc.a1 && angle <= arc.a2)
			return true;
	}

	return false;
}

std::vector<Eigen::Vector2f> getLinePixels(const cv::Mat& thresholded) {
	std::vector<Eigen::Vector2f> linePixels;
	for (int y = 0; y < thresholded.rows; y++) {
		for (int x = 0; x < thresholded.cols; x++) {
			if(thresholded.at<uint8_t>(y, x))
				linePixels.emplace_back(x, y);
		}
	}
	return linePixels;
}

int modelError(const Resources& r, const CameraModel& model, const std::vector<Eigen::Vector2f>& linePixels) {
	const SSL_GeometryFieldSize& field = r.socket->getGeometry().field();
	const float halfLineWidth = (float)field.line_thickness() / 2.0f;

	std::vector<std::pair<Eigen::Vector2f, Eigen::Vector2f>> lines;
	std::vector<LineArc> arcs;
	fieldToLines(r, lines, arcs);

	int error = 0.0;
	for(const Eigen::Vector2f& linePixel : linePixels) {
		if(!pointAtLine(model, lines, arcs, halfLineWidth, linePixel))
			error += 1;
	}
	return error;
}

static float modelError(const Resources& r, const CameraModel& model, const cv::Mat& thresholded) {
	std::vector<std::pair<Eigen::Vector2f, Eigen::Vector2f>> lines;
	std::vector<LineArc> arcs;
	fieldToLines(r, lines, arcs);

	int hit = 0;
	int miss = 0;
	const float halfLineWidth = (float)r.socket->getGeometry().field().line_thickness() / 2.0f;
	for(int y = 0; y < thresholded.rows; y++) {
		for(int x = 0; x < thresholded.cols; x++) {
			if(pointAtLine(model, lines, arcs, halfLineWidth, {x, y})) {
				if(thresholded.at<uint8_t>(y, x))
					hit++;
				else
					miss++;
			}
		}
	}

	return (float)miss / (float)(hit+miss);
}

static void drawModel(const Resources& r, cv::Mat& thresholded, const std::vector<Eigen::Vector2f>& linePixels, const CameraModel& model) {
	std::vector<std::pair<Eigen::Vector2f, Eigen::Vector2f>> lines;
	std::vector<LineArc> arcs;
	fieldToLines(r, lines, arcs);

	const float halfLineWidth = (float)r.socket->getGeometry().field().line_thickness() / 2.0f;
	for(int y = 0; y < thresholded.rows; y++) {
		for(int x = 0; x < thresholded.cols; x++) {
			thresholded.at<uint8_t>(y, x) = pointAtLine(model, lines, arcs, halfLineWidth, {(float)x, (float)y}) ? 170 : 0;
		}
	}

	for(const Eigen::Vector2f& linePixel : linePixels) {
		thresholded.at<uint8_t>((int)linePixel.y(), (int)linePixel.x()) = pointAtLine(model, lines, arcs, halfLineWidth, linePixel) ? 255 : 85;
	}
}


bool isClockwiseConvexQuadrilateral(const std::vector<Eigen::Vector2f>& vertexlist) {
	//Convexity check adapted from https://math.stackexchange.com/a/1745427 by Nominal Animal under CC BY-SA 3.0
	//Clockwise check adapted from https://stackoverflow.com/a/1165943 by Roberteo Bonvallet under CC BY-SA 3.0
	float clockwise = 0;

	float wSign = 0;

	int xSign = 0;
	int xFirstSign = 0;
	int xFlips = 0;

	int ySign = 0;
	int yFirstSign = 0;
	int yFlips = 0;

	Eigen::Vector2f curr = vertexlist.back(); //unused
	Eigen::Vector2f next = vertexlist.back();

	for(const Eigen::Vector2f& v : vertexlist) {
		Eigen::Vector2f prev = curr;
		curr = next;
		next = v;

		Eigen::Vector2f b = curr - prev;
		Eigen::Vector2f a = next - curr;

		clockwise += a[0] * (next[1]+curr[1]);

		if (a[0] > 0) {
			if (xSign == 0)
				xFirstSign = 1;
			else if (xSign < 0)
				xFlips++;

			xSign = 1;
		} else if (a[0] < 0) {
			if (xSign == 0)
				xFirstSign = -1;
			else if (xSign > 0)
				xFlips++;

			xSign = -1;
		}

		if (xFlips > 2)
			return false;

		if (a[1] > 0) {
			if (ySign == 0)
				yFirstSign = 1;
			else if (ySign < 0)
				yFlips++;

			ySign = 1;
		} else if (a[1] < 0) {
			if (ySign == 0)
				yFirstSign = -1;
			else if (ySign > 0)
				yFlips++;

			ySign = -1;
		}

		if (yFlips > 2)
			return false;

		float w = b[0]*a[1] - a[0]*b[1];
		if (wSign == 0 && w != 0)
			wSign = w;
		else if ((wSign > 0 && w < 0) || (wSign < 0 && w > 0))
			return false;
	}

	if (xSign != 0 && xFirstSign != 0 && xSign != xFirstSign)
		xFlips++;
	if (ySign != 0 && yFirstSign != 0 && ySign != yFirstSign)
		yFlips++;

	if (xFlips != 2 || yFlips != 2)
		return false;

	return clockwise < 0;
}

static void directCalibrationRefinement(const Resources& r, const std::vector<std::vector<Eigen::Vector2f>>& mergedPixels, const std::vector<Eigen::Vector2f>& linePixels, bool calibHeight, CameraModel& model) {
	DirectGeometryFit functor(r, linePixels, mergedPixels, model, calibHeight);
	Eigen::NumericalDiff<DirectGeometryFit> numDiff(functor);
	Eigen::LevenbergMarquardt<Eigen::NumericalDiff<DirectGeometryFit>> lm(numDiff);

	Eigen::VectorXf k(calibHeight ? 7 : 6);
	k[0] = model.focalLength;
	Eigen::Vector3f euler = model.getEuler();
	k[1] = euler.x();
	k[2] = euler.y();
	k[3] = euler.z();
	k[4] = model.pos.x();
	k[5] = model.pos.y();
	if(calibHeight)
		k[6] = model.pos.z();

	lm.minimize(k);

	if(lm.info() != Eigen::ComputationInfo::Success && lm.info() != Eigen::ComputationInfo::NoConvergence) { //xtol might be too aggressive
		std::cerr << "[Geometry calibration] Unable to find matching field model, aborting calibration for this frame. (lm.info() no success)" << std::endl;
		return;
	}

	if(calibHeight && k[6] < 0) { // camera below field
		std::cerr << "[Geometry calibration] Unable to find matching field model, aborting calibration for this frame. (camera below field)" << std::endl;
		return;
	}

	model.focalLength = k[0];
	model.updateEuler({k[1], k[2], k[3]});
	model.pos.x() = k[4];
	model.pos.y() = k[5];
	if(calibHeight)
		model.pos.z() = k[6];

	if(model.focalLength < 0) {
		model.focalLength = -k[0];
		model.f2iOrientation = Eigen::AngleAxisf(M_PI_2, Eigen::Vector3f::UnitZ()) * model.f2iOrientation;
	}
}

struct PointGeometryFit : public Eigen::DenseFunctor<float> {
	const std::vector<Eigen::Vector2f>& imageCorners;
	const std::vector<std::vector<Eigen::Vector2f>>& mergedPixels;
	const CameraModel& reference;
	const bool calibHeight;
	const bool calibDistortion;

	std::vector<Eigen::Vector2f> modelCorners;

	explicit PointGeometryFit(const Resources& r, const std::vector<Eigen::Vector2f>& imageCorners, const std::vector<std::vector<Eigen::Vector2f>>& mergedPixels, const CameraModel& model, const bool calibHeight, const bool calibDistortion): imageCorners(imageCorners), mergedPixels(mergedPixels), reference(model), calibHeight(calibHeight), calibDistortion(calibDistortion) {
		Eigen::Vector2f extentMin;
		Eigen::Vector2f extentMax;
		visibleFieldExtent(r, false, extentMin, extentMax);
		modelCorners.push_back(extentMin);
		modelCorners.emplace_back(extentMin.x(), extentMax.y());
		modelCorners.push_back(extentMax);
		modelCorners.emplace_back(extentMax.x(), extentMin.y());
	}

	int operator()(const InputType &x, ValueType& fvec) const {
		CameraModel model = reference;
		model.focalLength = x[0];
		model.updateEuler({x[1], x[2], x[3]});
		model.pos.x() = x[4];
		model.pos.y() = x[5];
		if(calibHeight)
			model.pos.z() = x[6];
		if(calibDistortion)
			calibrateDistortion(mergedPixels, model);
		model.updateDerived();

		for(unsigned int i = 0; i < modelCorners.size(); i++) {
			Eigen::Vector2f image = model.field2image({modelCorners[i].x(), modelCorners[i].y(), 0.f});
			fvec[2*i] = imageCorners[i].x() - image.x();
			fvec[2*i+1] = imageCorners[i].y() - image.y();
		}

		return 0;
	}

	int values() const {
		return 2*modelCorners.size();
	}
};

static bool cornerCalibration(const Resources& r, const std::vector<std::vector<Eigen::Vector2f>>& mergedPixels, const cv::Mat& thresholded, bool calibHeight, CameraModel& basicModel) {
	std::vector<Eigen::Vector2f> edges = r.lineCorners;
	std::sort(edges.begin(), edges.end(), [](const auto& l, const auto& r){ return r.y() > l.y() || (r.y() == l.y() && r.x() > l.x()); });
	if(edges.size() != 4) {
		std::cerr << "[Geometry calibration] Wrong line corner amount: " << edges.size() << "/4" << std::endl;
		return false;
	}

	float minError = INFINITY;
	CameraModel minModel;

	do {
		if(!isClockwiseConvexQuadrilateral(edges))
			continue;

		// Ensure 3D cartesian coordinates match camera orientation on single camera fields (resolve ambiguity)
		if(r.cameraAmount == 1 && (edges[0] + edges[3]).y() < (edges[1] + edges[2]).y())
			continue;

		//Ensure first point is minmin point
		if(r.lineCorners[0] != edges[0])
			continue;

		CameraModel model = basicModel;

		for(int i = 0; i < 10; i++) {
			calibrateDistortion(mergedPixels, model);

			PointGeometryFit functor(r, edges, mergedPixels, model, calibHeight, false); //calibDistortion
			Eigen::NumericalDiff<PointGeometryFit> numDiff(functor);
			Eigen::LevenbergMarquardt<Eigen::NumericalDiff<PointGeometryFit>> lm(numDiff);

			Eigen::VectorXf k(calibHeight ? 7 : 6);
			k[0] = model.focalLength;
			Eigen::Vector3f euler = model.getEuler();
			k[1] = euler.x();
			k[2] = euler.y();
			k[3] = euler.z();
			k[4] = model.pos.x();
			k[5] = model.pos.y();
			if(calibHeight)
				k[6] = model.pos.z();

			lm.minimize(k);

			if(lm.info() != Eigen::ComputationInfo::Success && lm.info() != Eigen::ComputationInfo::NoConvergence) //xtol might be too aggressive
				continue;

			if(calibHeight && k[6] < 0) // camera below field
				continue;

			model.focalLength = k[0];
			model.updateEuler({k[1], k[2], k[3]});
			model.pos.x() = k[4];
			model.pos.y() = k[5];
			if(calibHeight)
				model.pos.z() = k[6];

			if(model.focalLength < 0) {
				model.focalLength = -k[0];
				model.f2iOrientation = Eigen::AngleAxisf(M_PI_2, Eigen::Vector3f::UnitZ()) * model.f2iOrientation;
			}

			model.updateDerived();
		}

		float error = modelError(r, model, thresholded);
		if(error < minError) {
			minError = error;
			minModel = model;
		}

		} while(std::next_permutation(edges.begin(), edges.end(), [](const auto& l, const auto& r){ return r.y() > l.y() || (r.y() == l.y() && r.x() > l.x()); }));

	if(minError == INFINITY) {
		std::cerr << "[Geometry calibration] Unable to find matching field model, aborting calibration for this frame." << std::endl;
		return false;
	}

	basicModel = minModel;
	return true;
}

void geometryCalibration(const Resources& r, const CLImage& rgba) {
	rgba.save(".geomcalib_input.png");

	cv::Mat bgr;
	cv::cvtColor(rgba.read<RGBA>().cv, bgr, cv::COLOR_RGBA2BGR);
	cv::Mat gray;
	cv::cvtColor(rgba.read<RGBA>().cv, gray, cv::COLOR_RGBA2GRAY);

	const int halfLineWidth = halfLineWidthEstimation(r, gray);
	std::cout << "[Geometry calibration] Half line width: " << halfLineWidth << std::endl;

	cv::Mat thresholded(gray.rows, gray.cols, CV_8UC1);
	thresholdImage(r, gray, halfLineWidth, thresholded);
	cv::imwrite("img/" + rgba.name + ".pixels.png", thresholded);

	const std::vector<Eigen::Vector2f> linePixels = getLinePixels(thresholded);

	cv::Ptr<cv::LineSegmentDetector> detector = cv::createLineSegmentDetector();
	cv::Mat4f linesMat;
	detector->detect(thresholded, linesMat);
	detector->drawSegments(bgr, linesMat);

	CVLines lines;
	for(int i = 0; i < linesMat.rows; i++) {
		cv::Vec4f line = linesMat(0, i);
		cv::Vec2f a = cv::Vec2f(line[0], line[1]);
		cv::Vec2f b = cv::Vec2f(line[2], line[3]);
		if(dist(a, b) >= r.minLineSegmentLength)
			lines.emplace_back(a, b);
	}
	std::cout << "[Geometry calibration] Line segments: " << lines.size() << std::endl;

	const std::vector<CVLines> compoundLines = groupLineSegments(r, lines);
	const CVLines mergedLines = mergeLineSegments(compoundLines);
	std::cout << "[Geometry calibration] Lines: " << mergedLines.size() << std::endl;

	std::vector<std::vector<Eigen::Vector2f>> mergedPixels(mergedLines.size());
	{
		const float sqHalfLineWidth = (float)(halfLineWidth*halfLineWidth);
		for (int y = 0; y < thresholded.rows; y++) {
			for (int x = 0; x < thresholded.cols; x++) {
				if(thresholded.at<uint8_t>(y, x)) {
					for(unsigned int i = 0; i < compoundLines.size(); i++) {
						if(dist(mergedLines[i].first, mergedLines[i].second) < thresholded.rows/2)
							continue;

						for(const auto& segment : compoundLines[i]) {
							if(sqPointLineSegmentDistance(std::make_pair(cv2eigen(segment.first), cv2eigen(segment.second)), {x, y}) <= sqHalfLineWidth) {
								mergedPixels[i].emplace_back(x, y);
							}
						}
					}
				}
			}
		}
	}
	mergedPixels.erase(std::remove_if(mergedPixels.begin(), mergedPixels.end(), [](const auto& v) { return v.empty(); }), mergedPixels.end());

	for (const auto& item : mergedLines) {
		cv::line(bgr, {item.first}, {item.second}, CV_RGB(0, 255, 0));
	}
	cv::imwrite("img/" + rgba.name + ".lines.png", bgr);

	const bool calibHeight = r.cameraHeight == 0.0;
	CameraModel model({thresholded.cols, thresholded.rows}, r.camId, r.cameraAmount, (float)r.cameraHeight, r.socket->getGeometry().field());
	//drawModel(r, thresholded, linePixels, model);
	//thresholded.save(".initial.png");

	cornerCalibration(r, mergedPixels, thresholded, calibHeight, model);
	drawModel(r, thresholded, linePixels, model);
	cv::imwrite("img/" + rgba.name + ".pixels.corner.png", thresholded);

	if(r.geometryRefinement)
		directCalibrationRefinement(r, mergedPixels, linePixels, calibHeight, model);

	model.updateDerived();
	int error = modelError(r, model, linePixels);
	std::cout << "[Geometry calibration] Best model: " << model << " error " << (error/(float)linePixels.size()) << std::endl;

	SSL_WrapperPacket wrapper;
	wrapper.mutable_geometry()->CopyFrom(r.socket->getGeometry());
	wrapper.mutable_geometry()->clear_calib();
	wrapper.mutable_geometry()->add_calib()->CopyFrom(model.getProto(r.camId));
	r.socket->send(wrapper);

	drawModel(r, thresholded, linePixels, model);
	cv::imwrite("img/" + rgba.name + ".pixels.refined.png", thresholded);
}