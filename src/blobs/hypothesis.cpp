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
#include "hypothesis.h"
#include "kmeans.h"
#include "pattern.h"


//atan2 implementation adapted from: https://mazzo.li/posts/vectorized-atan2.html https://gist.github.com/bitonic/d0f5a0a44e37d4f0be03d34d47acb6cf
static inline float atan_fma_approximation(float x) {
	float a1  =  0.99997726f;
	float a3  = -0.33262347f;
	float a5  =  0.19354346f;
	float a7  = -0.11643287f;
	float a9  =  0.05265332f;
	float a11 = -0.01172120f;

	// Compute approximation using Horner's method
	float x_sq = x*x;
	return x * fmaf(x_sq, fmaf(x_sq, fmaf(x_sq, fmaf(x_sq, fmaf(x_sq, a11, a9), a7), a5), a3), a1);
}

float atan2_fast(const float y, const float x) {
	const float pi = M_PI;
	const float pi_2 = M_PI_2;

	// Ensure input is in [-1, +1]
	bool swap = fabsf(x) < fabsf(y);
	float divisor = (swap ? y : x);
	if(divisor == 0.0) //TODO look further why this extra check is necessary (why both arguments are 0)
		return swap ? x : y;

	const float atan_input = (swap ? x : y) / divisor;

	// Approximate atan
	float res = atan_fma_approximation(atan_input);

	// If swapped, adjust atan output
	res = swap ? copysignf(pi_2, atan_input) - res : res;
	// Adjust the result depending on the input quadrant
	if (x < 0.0f) {
		res = copysignf(pi, y) + res;
	}

	return res;
}


BallHypothesis::BallHypothesis(const Resources& r, const Match* blob): blob(blob), pos(blob->pos) {
	calcColorScore(r);
}

void BallHypothesis::recalcPostColorCalib(const Resources& r) {
	score = 1.0f;
	calcColorScore(r);
}

void BallHypothesis::addToDetectionFrame(const Resources& r, SSL_DetectionFrame* detection) {
	const Eigen::Vector2f imgPos = r.perspective->model.field2image({pos.x(), pos.y(), (float)r.gcSocket->maxBotHeight});
	const Eigen::Vector3f ballPos = r.perspective->model.image2field(imgPos, r.perspective->field.ball_radius());
	SSL_DetectionBall* ball = detection->add_balls();
	ball->set_confidence(score);
	//ball->set_area(0);
	ball->set_x(ballPos.x());
	ball->set_y(ballPos.y());
	//ball->set_z(0.0f);
	ball->set_pixel_x(imgPos.x());
	ball->set_pixel_y(imgPos.y());
}

void BallHypothesis::calcColorScore(const Resources& r) {
	int falseOrange = (blob->color - r.field).squaredNorm();
	int orange = (blob->color - r.orange).squaredNorm();

	if (falseOrange <= orange) {
		score = 0;
		return;
	}

	score *= 1 - (float)orange / (float)falseOrange;
}


BotHypothesis::BotHypothesis(const Match* a, const Match* b, const Match* c, const Match* d, const Match* e): blobs{a, b, c, d, e} {
	for(auto& blob : blobs)
		if(blob != nullptr)
			blobAmount++;

	calcPos();
	calcOffsetScore();
}

bool BotHypothesis::isClipping(const Resources& r, const BotHypothesis& other) const {
	Eigen::Vector2f diff = other.pos - pos;
	float sqDistance = diff.squaredNorm();
	if(sqDistance >= (2*MIN_ROBOT_RADIUS)*(2*MIN_ROBOT_RADIUS)) //Early rejection for faster calculation (simple circle - circle clipping)
		return false;

	float diffAngle = atan2_fast(diff.y(), diff.x());
	float selfAngle = remainderf(diffAngle - orientation, 2.0f * M_PI);
	float otherAngle = remainderf(diffAngle - other.orientation, 2.0f * M_PI);

	float minDistance =
			(abs(selfAngle) < MIN_ROBOT_OPENING_ANGLE ? MIN_ROBOT_FRONT_DISTANCE/cosf(selfAngle) : MIN_ROBOT_RADIUS) +
			(abs(otherAngle) < MIN_ROBOT_OPENING_ANGLE ? MIN_ROBOT_FRONT_DISTANCE/cosf(otherAngle) : MIN_ROBOT_RADIUS)
			- r.clippingTolerance;

	return sqDistance < minDistance*minDistance;
}

bool BotHypothesis::isClipping(const Resources& r, const BallHypothesis& ball) const {
	const float clippedBallRadius = 0.48837f * r.perspective->field.ball_radius(); // a ball may clip up to 20% of the top-view area into the robot
	Eigen::Vector2f diff = ball.pos - pos;
	float sqDistance = diff.squaredNorm();
	float minDistance = MIN_ROBOT_RADIUS + clippedBallRadius;
	if(sqDistance >= minDistance*minDistance)
		return false;

	float angle = remainderf(atan2_fast(diff.y(), diff.x()) - orientation, 2.0f * M_PI);
	if(abs(angle) >= MIN_ROBOT_OPENING_ANGLE)
		return true;

	minDistance = (MIN_ROBOT_FRONT_DISTANCE + clippedBallRadius) / cosf(angle) - r.clippingTolerance;

	return sqDistance < minDistance*minDistance;
}

void BotHypothesis::addToDetectionFrame(const Resources& r, SSL_DetectionFrame* detection) {
	bool yellow = botId < 16;
	const Eigen::Vector2f imgPos = r.perspective->model.field2image({pos.x(), pos.y(), (float)r.gcSocket->maxBotHeight});
	const Eigen::Vector3f botPos = r.perspective->model.image2field(imgPos, (float)(yellow ? r.gcSocket->yellowBotHeight : r.gcSocket->blueBotHeight));
	SSL_DetectionRobot* bot = yellow ? detection->add_robots_yellow() : detection->add_robots_blue();
	bot->set_confidence(score);
	bot->set_robot_id(botId % 16);
	bot->set_x(botPos.x());
	bot->set_y(botPos.y());
	bot->set_height(botPos.z());
	bot->set_orientation(orientation);
	bot->set_pixel_x(imgPos.x());
	bot->set_pixel_y(imgPos.y());
}

void BotHypothesis::calcPos() {
	//https://www.themathdoctors.org/averaging-angles/
	float oSin = 0;
	float oCos = 0;
	for(int a = 0; a < 5; a++) {
		if(blobs[a] == nullptr)
			continue;

		for(int b = a+1; b < 5; b++) {
			if(blobs[b] == nullptr)
				continue;

			Eigen::Vector2f diff = blobs[b]->pos - blobs[a]->pos;
			float angleDelta = atan2_fast(diff.y(), diff.x()) - patternAnglesb2b[b*5 + a];
			oSin += sinf(angleDelta);
			oCos += cosf(angleDelta);
		}
	}
	if(blobAmount < 1)
		return;

	if(blobAmount > 1)
		orientation = atan2_fast(oSin, oCos);

	Eigen::Rotation2Df rotation(orientation);
	pos.x() = 0;
	pos.y() = 0;
	for(int i = 0; i < 5; i++) {
		if(blobs[i] == nullptr)
			continue;

		pos += blobs[i]->pos - rotation * patternPos[i];
	}

	pos /= (float)blobAmount;
}

void BotHypothesis::calcOffsetScore() {
	Eigen::Rotation2Df rotation(orientation);
	for(int i = 0; i < 5; i++) {
		const Match* const& blob = blobs[i];
		if(blob == nullptr)
			continue;

		Eigen::Vector2f offset = (blob->pos - (pos + rotation * patternPos[i])) / 10.0f; // (10.0f) 1cm offset -> 0.5 score
		offsetScore = std::min(offsetScore, 1 / (1 + offset.squaredNorm()));
	}

	score = offsetScore;
}


DetectionBotHypothesis::DetectionBotHypothesis(const Resources& r, const Match* a, const Match* b, const Match* c, const Match* d, const Match* e): BotHypothesis(a, b, c, d, e) {
	calcBotId(r);
}

void DetectionBotHypothesis::recalcPostColorCalib(const Resources& r) {
	calcBotId(r);
}

void DetectionBotHypothesis::calcBotId(const Resources& r) {
	Eigen::Vector3i green = r.green;
	Eigen::Vector3i pink = r.pink;
	kMeans(blobs[0]->color, {blobs[1]->color, blobs[2]->color, blobs[3]->color, blobs[4]->color}, green, pink);

	botId = ((blobs[0]->color - r.blue).squaredNorm() < (blobs[0]->color - r.yellow).squaredNorm() ? 16 : 0) + patternLUT[
			(((blobs[1]->color - green).squaredNorm() < (blobs[1]->color - pink).squaredNorm() ? 1 : 0) << 3) +
			(((blobs[2]->color - green).squaredNorm() < (blobs[2]->color - pink).squaredNorm()? 1 : 0) << 2) +
			(((blobs[3]->color - green).squaredNorm() < (blobs[3]->color - pink).squaredNorm() ? 1 : 0) << 1) +
			((blobs[4]->color - green).squaredNorm() < (blobs[4]->color - pink).squaredNorm() ? 1 : 0)
	];
}


TrackedBotHypothesis::TrackedBotHypothesis(const Resources& r, const TrackingState& tracked, const Eigen::Vector3f& trackedPosition, const Match* a, const Match* b, const Match* c, const Match* d, const Match* e): BotHypothesis(a, b, c, d, e), trackedScore(tracked.confidence), trackedPosition(trackedPosition) {
	botId = tracked.id;

	float rotationOffset = remainderf(orientation - trackedPosition.z(), 2.0f * M_PI) / (float)M_PI;
	offsetScore *= 1 / (1 + ((pos - trackedPosition.head<2>()) / 10.0f).squaredNorm() + rotationOffset*rotationOffset); // (10.0f) 1cm offset -> 0.5 score
	offsetScore *= (float)blobAmount / 5.f;

	TrackedBotHypothesis::recalcPostColorCalib(r);
}

void TrackedBotHypothesis::recalcPostColorCalib(const Resources& r) {
	score = offsetScore;
	calcTrackingScore(r);
}

void TrackedBotHypothesis::calcTrackingScore(const Resources& r) {
	if(blobAmount < 2) {
		score = 0.0f;
		return;
	}

	for(int i = 0; i < 5; i++) {
		const Match* const& blob = blobs[i];
		if(blob == nullptr)
			continue;

		Eigen::Vector3i blobColor;
		Eigen::Vector3i oppositeColor;
		if(i == 0) {
			blobColor = botId >= 16 ? r.blue : r.yellow;
			oppositeColor = botId >= 16 ? r.yellow : r.blue;
		} else {
			blobColor = ((patterns[botId % 16] >> (4-i)) & 1) ? r.green : r.pink;
			oppositeColor = ((patterns[botId % 16] >> (4-i)) & 1) ? r.pink : r.green;
		}

		if((blob->color - oppositeColor).squaredNorm() - (blob->color - blobColor).squaredNorm() <= 0) {
			score = 0.0f;
			return;
		}
	}
}