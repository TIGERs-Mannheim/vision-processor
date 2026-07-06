#include "colorupdate.h"
#include "kmeans.h"
#include "pattern.h"


static float sqPointLineSegmentDistance(const SSL_FieldLineSegment& line, const Eigen::Vector2f& point) {
	//Adapted from Grumdrig https://stackoverflow.com/a/1501725 CC BY-SA 4.0
	Eigen::Vector2f p1(line.p1().x(), line.p1().y());
	Eigen::Vector2f p2(line.p2().x(), line.p2().y());
	const Eigen::Vector2f v = p2 - p1;
	const Eigen::Vector2f w = point - p1;
	const float t = std::max(0.0f, std::min(1.0f, w.dot(v) / v.dot(v)));
	const Eigen::Vector2f delta = w - t * v;
	return delta.dot(delta);
}

static bool ballAtLine(const Resources& r, const BallHypothesis& ball) {
	const Eigen::Vector2f imgPos = r.perspective->model.field2image({ball.pos.x(), ball.pos.y(), (float)r.gcSocket->maxBotHeight});
	const Eigen::Vector2f ballPos = r.perspective->model.image2field(imgPos, r.perspective->field.ball_radius()).head<2>();

	const float maxLineDistance = (float)r.perspective->field.line_thickness()/2 + r.geometryTolerance;
	const float sqMaxLineDistance = maxLineDistance*maxLineDistance;

	for(const auto& line : r.perspective->field.field_lines()) {
		if(sqPointLineSegmentDistance(line, ballPos) <= sqMaxLineDistance)
			return true;
	}

	for(const auto& arc : r.perspective->field.field_arcs()) {
		const Eigen::Vector2f pixel2center = ballPos - Eigen::Vector2f(arc.center().x(), arc.center().y());
		float angle = atan2f(pixel2center.y(), pixel2center.x());
		if(angle < 0)
			angle += 2*M_PI;

		if(abs(sqrtf(pixel2center.dot(pixel2center)) - arc.radius()) <= maxLineDistance && angle >= arc.a1() && angle <= arc.a2())
			return true;
	}

	return false;
}

static void determineFieldLineBlobColor(Resources& r, const std::list<std::unique_ptr<BallHypothesis>>& ballHypotheses) {
	Eigen::Vector3i colorSum(0, 0, 0);
	int amount = 0;

	for (const auto& ball : ballHypotheses) {
		if(ballAtLine(r, *ball)) {
			colorSum += ball->blob->color;
			amount++;
		}
	}

	//TODO median instead?
	if(amount > 2)
		r.fieldLineColor = colorSum /= amount;
}

static inline void updateColor(const Resources& r, const Eigen::Vector3i& reference, const Eigen::Vector3i& oldColor, Eigen::Vector3i& color) {
	const float updateForce = 1.0f - r.referenceForce - r.historyForce;
	color = (r.referenceForce*reference.cast<float>() + r.historyForce*oldColor.cast<float>() + updateForce*color.cast<float>()).cast<int>();
}

void updateColors(Resources& r, const std::list<std::unique_ptr<BotHypothesis>>& bestBotModels, const std::list<std::unique_ptr<BallHypothesis>>& ballCandidates) {
	Eigen::Vector3i oldField = r.field;
	Eigen::Vector3i oldOrange = r.orange;
	Eigen::Vector3i oldYellow = r.yellow;
	Eigen::Vector3i oldBlue = r.blue;
	Eigen::Vector3i oldGreen = r.green;
	Eigen::Vector3i oldPink = r.pink;

	std::vector<Eigen::Vector3i> centerBlobs;
	Eigen::Vector3i pink(0, 0, 0);
	int pinkN = 0;
	Eigen::Vector3i green(0, 0, 0);
	int greenN = 0;
	for (const auto& model : bestBotModels) {
		if(model->blobs[0] != nullptr)
			centerBlobs.push_back(model->blobs[0]->color);

		int botId = model->botId % 16;
		for(int i = 1; i < 5; i++) {
			const Match* blob = model->blobs[i];
			if(blob == nullptr)
				continue;

			if((patterns[botId] >> (4-i)) & 1) {
				green += blob->color;
				greenN++;
			} else {
				pink += blob->color;
				pinkN++;
			}
		}
	}

	if(pinkN > 0) {
		r.pink = pink / pinkN;
		updateColor(r, r.pinkReference, oldPink, r.pink);
	}
	if(greenN > 0) {
		r.green = green / greenN;
		updateColor(r, r.greenReference, oldGreen, r.green);
	}

	if(kMeans(r.pink, centerBlobs, r.yellow, r.blue)) {
		updateColor(r, r.yellowReference, oldYellow, r.yellow);
		updateColor(r, r.blueReference, oldBlue, r.blue);
	}

	std::vector<Eigen::Vector3i> ballBlobs;
	for (const auto& ball : ballCandidates)
		ballBlobs.push_back(ball->blob->center);

	if(kMeans(r.blue, ballBlobs, r.orange, r.field)) {
		updateColor(r, r.orangeReference, oldOrange, r.orange);
		updateColor(r, r.fieldReference, oldField, r.field);
	}

	determineFieldLineBlobColor(r, ballCandidates);
}
