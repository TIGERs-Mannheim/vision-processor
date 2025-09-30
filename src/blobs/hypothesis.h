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


#include "match.h"
#include "Resources.h"

float atan2_fast(float y, float x);


class BallHypothesis {
public:
	BallHypothesis(const Resources& r, const Match* blob);

	virtual void recalcPostColorCalib(const Resources& r);

	void addToDetectionFrame(const Resources& r, SSL_DetectionFrame* detection);

	const Match* blob;
	Eigen::Vector2f pos = {0, 0};
	float score = 1.0f;

private:
	void calcColorScore(const Resources& r);
};


class BotHypothesis {
public:
	BotHypothesis(const Match* a, const Match* b, const Match* c, const Match* d, const Match* e);

	[[nodiscard]] bool isClipping(const Resources& r, const BotHypothesis& other) const;

	[[nodiscard]] bool isClipping(const Resources& r, const BallHypothesis& ball) const;

	void addToDetectionFrame(const Resources& r, SSL_DetectionFrame* detection);

	virtual void recalcPostColorCalib(const Resources& r) = 0;

	const Match* blobs[5];
	Eigen::Vector2f pos = {0, 0};
	float orientation = 0;
	float score = 1.0f;
	float offsetScore = 1.0f;
	int botId = -1;
	int blobAmount = 0;

private:
	inline void calcPos();

	inline void calcOffsetScore();
};


class DetectionBotHypothesis: public BotHypothesis {
public:
	DetectionBotHypothesis(const Resources& r, const Match* a, const Match* b, const Match* c, const Match* d, const Match* e);

	void recalcPostColorCalib(const Resources &r) override;

private:
	inline void calcBotId(const Resources& r);
};


class TrackedBotHypothesis: public BotHypothesis {
public:
	TrackedBotHypothesis(const Resources& r, const TrackingState& tracked, const Eigen::Vector3f& trackedPosition, const Match* a, const Match* b, const Match* c, const Match* d, const Match* e);

	void recalcPostColorCalib(const Resources &r) override;

private:
	inline void calcTrackingScore(const Resources& r);

	const float trackedScore;
	const Eigen::Vector3f trackedPosition;
};
