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
#include <csignal>
#include <iostream>
#include <iomanip>
#include <opencv2/bgsegm.hpp>
#include <yaml-cpp/yaml.h>

#include "proto/ssl_vision_wrapper.pb.h"
#include "Resources.h"
#include "GroundTruth.h"
#include "calib/GeomModel.h"
#include "pattern.h"
#include "cl_kernels.h"
#include "blobs/hypothesis.h"
#include "blobs/kmeans.h"
#include "blobs/kdtree.h"
#include <opencv2/video/background_segm.hpp>

struct __attribute__ ((packed)) CLMatch {
	float x, y;
	RGB color;
	RGB center;
	float circ;
	float score;

	auto operator<=>(const CLMatch&) const = default;
};

void generateAngleSortedBotHypotheses(const Resources& r, std::list<std::unique_ptr<BotHypothesis>>& bots, std::vector<Match>& matches, KDTree& blobs) {
	std::vector<Match*> botBlobs;
	for(int i = 0; i < blobs.getSize(); i++) {
		Match& blob = matches[i];

		float bestBotScore = 0.0f;
		std::unique_ptr<BotHypothesis> bestBot = nullptr;

		botBlobs.clear();
		blobs.rangeSearch(botBlobs, blob.pos, r.perspective->field.max_robot_radius());
		if(botBlobs.size() < 4)
			continue;

		std::sort(botBlobs.begin(), botBlobs.end(), [&](const Match* a, const Match* b) -> bool {
			Eigen::Vector2f aDiff = a->pos - blob.pos;
			Eigen::Vector2f bDiff = b->pos - blob.pos;
			return atan2_fast(aDiff.y(), aDiff.x()) < atan2_fast(bDiff.y(), bDiff.x());
		});

		const int size = (int)botBlobs.size();
		for(int a = 0; a < size; a++) {
			for(int b = a+1; b < a+size-2; b++) {
				for(int c = b+1; c < a+size-1; c++) {
					for(int d = c+1; d < a+size; d++) {
						std::unique_ptr<BotHypothesis> bot = std::make_unique<DetectionBotHypothesis>(r, &blob, botBlobs[a], botBlobs[b%size], botBlobs[c%size], botBlobs[d%size]);
						if(bot->score > bestBotScore) {
							bestBotScore = bot->score;
							bestBot = std::move(bot);
						}
					}
				}
			}
		}

		bots.push_back(std::move(bestBot));
	}
}

void generateRadiusSearchTrackedBotHypotheses(const Resources& r, std::list<std::unique_ptr<BotHypothesis>>& bots, std::vector<Match>& matches, KDTree& blobs, const double currentTimestamp) {
	std::vector<Match*> botBlobs[5];
	for (const auto& camTracked : r.socket->getTrackedObjects()) {
		for (const auto& tracked : camTracked.second) {
			if(tracked.id == -1)
				continue;

			auto timeDelta = (float)(currentTimestamp - tracked.timestamp);
			Eigen::Vector2f reprojectedPosition = r.perspective->model.image2field(r.perspective->model.field2image({tracked.x, tracked.y, tracked.z}), r.gcSocket->maxBotHeight).head<2>();
			Eigen::Vector3f trackedPosition = Eigen::Vector3f(reprojectedPosition.x(), reprojectedPosition.y(), tracked.w) + Eigen::Vector3f(tracked.vx, tracked.vy, tracked.vw) * timeDelta;
			Eigen::Rotation2Df rotation(trackedPosition.z());

			//prevent runtime escalation due to excessive timeDelta when FPS drop below 20 FPS or times are not synced
			timeDelta = std::max(std::min(timeDelta, 0.05f), 0.0f);
			//Double acceleration due to velocity determination from two frame difference
			float blobSearchRadius = (float)r.maxBotAcceleration * timeDelta * timeDelta + (float)r.minTrackingRadius;

			float bestBotScore = 0.0f;
			std::unique_ptr<BotHypothesis> bestBot = nullptr;

			for(int i = 0; i < 5; i++) {
				botBlobs[i].clear();
				botBlobs[i].push_back(nullptr);
				blobs.rangeSearch(botBlobs[i], trackedPosition.head<2>() + rotation * patternPos[i], blobSearchRadius);
			}

			for(Match* const& a : botBlobs[0]) {
				for(Match* const& b : botBlobs[1]) {
					if(b != nullptr && a == b)
						continue;

					for(Match* const& c : botBlobs[2]) {
						if(c != nullptr && (a == c || b == c))
							continue;

						for(Match* const& d : botBlobs[3]) {
							if(d != nullptr && (a == d || b == d || c == d))
								continue;

							for(Match* const& e : botBlobs[4]) {
								if (e != nullptr && (a == e || b == e || c == e || d == e))
									continue;

								std::unique_ptr<BotHypothesis> bot = std::make_unique<TrackedBotHypothesis>(r, tracked, trackedPosition, a, b, c, d, e);
								if(bot->score > bestBotScore) {
									bestBotScore = bot->score;
									bestBot = std::move(bot);
								}
							}
						}
					}
				}
			}

			if(bestBot == nullptr)
				continue;

			bots.push_back(std::move(bestBot));
		}
	}
}

template<typename T>
void filterHypothesesScore(std::list<std::unique_ptr<T>>& bots, float threshold) {
	for(auto it = bots.cbegin(); it != bots.cend(); ) {
		if((*it)->score <= threshold) {
			it = bots.erase(it);
		} else {
			it++;
		}
	}
}

template<typename T>
void filterStddevScore(std::list<std::unique_ptr<T>>& bots, float threshold) {
	for(auto it = bots.cbegin(); it != bots.cend(); ) {
		if((*it)->blob->score <= threshold) {
			it = bots.erase(it);
		} else {
			it++;
		}
	}
}

void filterBallsAtCamEdge(const Resources& r, std::list<std::unique_ptr<BallHypothesis>>& balls) {
	const SSL_GeometryFieldSize& field = r.perspective->field;
	const float halfLength = (float)field.field_length()/2.0f + (float)field.boundary_width();
	const float halfWidth = (float)field.field_width()/2.0f + (float)field.boundary_width();

	const Eigen::Vector4f& visibleFieldExtent = r.perspective->visibleFieldExtent; // xmin, xmax, ymin, ymax
	bool xMinIsEdge = visibleFieldExtent[0] != -halfLength;
	bool xMaxIsEdge = visibleFieldExtent[1] != halfLength;
	bool yMinIsEdge = visibleFieldExtent[2] != -halfWidth;
	bool yMaxIsEdge = visibleFieldExtent[3] != halfWidth;

	for(auto it = balls.cbegin(); it != balls.cend(); ) {
		const Eigen::Vector2f& pos = (*it)->pos;
		if(
				(xMinIsEdge && abs(pos.x() - visibleFieldExtent[0]) <= r.minCamEdgeDistance) ||
				(xMaxIsEdge && abs(pos.x() - visibleFieldExtent[1]) <= r.minCamEdgeDistance) ||
				(yMinIsEdge && abs(pos.y() - visibleFieldExtent[2]) <= r.minCamEdgeDistance) ||
				(yMaxIsEdge && abs(pos.y() - visibleFieldExtent[3]) <= r.minCamEdgeDistance)
		) {
			it = balls.erase(it);
		} else {
			it++;
		}
	}
}

void filterClippingBotBotHypotheses(std::list<std::unique_ptr<BotHypothesis>>& bots) {
	for (auto it1 = bots.cbegin(); it1 != bots.cend(); ) {
		const auto& bot1 = *it1;
		bool remove = false;
		for (auto it2 = bots.cbegin(); it2 != bots.cend(); it2++) {
			const auto& bot2 = *it1;
			if (bot2->score > bot1->score && bot1->isClipping(*bot2)) {
				remove = true;
				break;
			}
		}

		if(remove) {
			it1 = bots.erase(it1);
			continue;
		}

		for (auto it2 = bots.cbegin(); it2 != bots.cend(); ) {
			const auto& bot2 = *it2;
			if (bot2->score <= bot1->score && bot1->isClipping(*bot2) && it1 != it2) {
				it2 = bots.erase(it2);
			} else {
				it2++;
			}
		}

		it1++;
	}
}

void generateNonclippingBallHypotheses(const Resources& r, const std::list<std::unique_ptr<BotHypothesis>>& bots, std::vector<Match>& matches, std::list<std::unique_ptr<BallHypothesis>>& balls) {
	for (const auto& match : matches) {
		std::unique_ptr<BallHypothesis> ball = std::make_unique<BallHypothesis>(r, &match);
		bool nextToBot = false;
		for (const auto& bot : bots) {
			if (bot->isClipping(r, *ball)) {
				nextToBot = true;
				break;
			}
		}

		if(nextToBot)
			continue;

		balls.push_back(std::move(ball));
	}
}

static inline void updateColor(const Resources& r, const Eigen::Vector3i& reference, const Eigen::Vector3i& oldColor, Eigen::Vector3i& color) {
	const float updateForce = 1.0f - r.referenceForce - r.historyForce;
	color = (r.referenceForce*reference.cast<float>() + r.historyForce*oldColor.cast<float>() + updateForce*color.cast<float>()).cast<int>();
}

static void updateColors(Resources& r, const std::list<std::unique_ptr<BotHypothesis>>& bestBotModels, const std::list<std::unique_ptr<BallHypothesis>>& ballCandidates) {
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
}

#define BENCHMARK false

static volatile bool noSigterm = true;
void sig_stop(int sig_num) {
	noSigterm = false;
}

int main(int argc, char* argv[]) {
	Resources r(YAML::LoadFile(argc > 1 ? argv[1] : "config.yml"));
	cl::Kernel blobList = r.openCl->compile(kernel_blobList_cl);

	uint32_t frameId = 0;
	CLArray matchArray(sizeof(CLMatch) * r.maxBlobs);
	CLArray counter(sizeof(cl_int)*3);

	signal(SIGTERM, sig_stop);
	signal(SIGINT, sig_stop);
	while(noSigterm) {
		frameId++;
		std::shared_ptr<RawImage> img = r.camera->readImage();
		if(img == nullptr)
			break;

		double startTime = r.camera->getTime();
		double realStartTime = getRealTime(); // Just for realtime performance measurements

		r.socket->geometryCheck();
		r.perspective->geometryCheck(img->width, img->height, r.gcSocket->maxBotHeight, r.resamplingFactor);
		std::shared_ptr<CLImage> channels[4];
		r.raw2quad(*img, channels);

		if(r.perspective->geometryVersion) {
			std::shared_ptr<CLImage> flat;
			std::shared_ptr<CLImage> gradDot;
			std::shared_ptr<CLImage> blobCenter;
			r.rgba2blobCenter(channels, flat, gradDot, blobCenter);

			{
				CLMap<int> counterMap = counter.write<int>();
				counterMap[0] = 0;
				counterMap[1] = 0;
				counterMap[2] = 0;
			}
			OpenCL::await(blobList, cl::EnqueueArgs(cl::NDRange(r.perspective->reprojectedFieldSize[0], r.perspective->reprojectedFieldSize[1])), flat->image, blobCenter->image, matchArray.buffer, counter.buffer, (float)r.minCircularity, (float)0.0f, (int)floorf(r.perspective->minBlobRadius / r.perspective->fieldScale), r.maxBlobs);

			if(r.debugImages && frameId == 1) {
				flat->save(".flat." + std::to_string(frameId) + ".png");
				gradDot->save(".gradDot." + std::to_string(frameId) + ".png", 0.25f, 128.f);
				blobCenter->save(".blob." + std::to_string(frameId) + ".png");
			}

			std::vector<Match> matches; //Same lifetime as KDTree required
			{
				CLMap<int> counterMap = counter.read<int>();
				CLMap<CLMatch> matchMap = matchArray.read<CLMatch>();
				const int matchAmount = std::min(r.maxBlobs, counterMap[0]);
				matches.reserve(matchAmount);

				for(int i = 0; i < matchAmount; i++) {
					CLMatch& match = matchMap[i];
					matches.push_back({
						.pos = r.perspective->flat2field({match.x, match.y}),
						.color = {match.color.r, match.color.g, match.color.b},
						.center = {match.center.r, match.center.g, match.center.b},
						.circ = match.circ,
						.score = match.score
					});
				}

				if(counterMap[0] > r.maxBlobs)
					std::cerr << "[main] max blob amount reached: " << counterMap[0] << "/" << r.maxBlobs << std::endl;
			}

			std::list<std::unique_ptr<BotHypothesis>> botHypotheses;
			std::list<std::unique_ptr<BallHypothesis>> ballHypotheses;

			if(!matches.empty()) {
				KDTree blobs = KDTree(&matches[0]);
				for(unsigned int i = 1; i < matches.size(); i++)
					blobs.insert(&matches[i]);

				generateRadiusSearchTrackedBotHypotheses(r, botHypotheses, matches, blobs, startTime);
				generateAngleSortedBotHypotheses(r, botHypotheses, matches, blobs);
				filterHypothesesScore(botHypotheses, r.minConfidence);
				filterClippingBotBotHypotheses(botHypotheses);
				generateNonclippingBallHypotheses(r, botHypotheses, matches, ballHypotheses);
			}

			updateColors(r, botHypotheses, ballHypotheses);
			for (auto& bot : botHypotheses)
				bot->recalcPostColorCalib(r);
			for (auto& ball : ballHypotheses)
				ball->recalcPostColorCalib(r);

			filterHypothesesScore(ballHypotheses, r.minConfidence);
			filterBallsAtCamEdge(r, ballHypotheses);
			filterStddevScore(ballHypotheses, (float)r.minScore);

			SSL_WrapperPacket wrapper;
			SSL_DetectionFrame* detection = wrapper.mutable_detection();
			detection->set_frame_number(frameId);
			detection->set_t_capture(startTime);
			if(img->timestamp != 0)
				detection->set_t_capture_camera(img->timestamp);
			detection->set_camera_id(r.camId);

			for (const auto& bot : botHypotheses)
				bot->addToDetectionFrame(r, detection);
			for (const auto& ball : ballHypotheses)
				ball->addToDetectionFrame(r, detection);

			for (const float& offset : r.socket->getReceivedOffsets())
				detection->add_t_offsets(offset);

			double processingTime = getRealTime() - realStartTime;

#if BENCHMARK
			detection->set_t_sent(startTime + processingTime);
			std::cout << "[main] time " << processingTime * 1000.0 << " ms " << matches.size() << " blobs " << detection->balls().size() << " balls " << (detection->robots_yellow_size() + detection->robots_blue_size()) << " bots" << std::endl;
#else
			detection->set_t_sent(r.camera->getTime());
#endif
			r.socket->send(wrapper);
			r.socket->updateTime();

			if(processingTime > r.camera->expectedFrametime())
				std::cout << "[main] frame time overrun: " << processingTime * 1000.0 << " ms " << matches.size() << " blobs " << detection->balls().size() << " balls " << (detection->robots_yellow_size() + detection->robots_blue_size()) << " bots" << std::endl;

			if(r.rawFeed) {
				r.streamQuad(channels);
			} else {
				switch(((long)(startTime/20.0) % 4)) {
					case 0:
						r.streamQuad(channels);
						break;
					case 1:
						r.streamImage(*flat);
						break;
					case 2:
						r.streamImage(*gradDot);
						break;
					case 3:
						r.streamImage(*blobCenter);
						break;
				}
			}
		} else if(r.socket->getGeometryVersion()) {
			geometryCalibration(r, *r.quad2rgba(channels));
		} else {
			r.streamQuad(channels);

			if(frameId == 100) {  // Wait for automatic gain, exposure and white balance adjustments
				r.quad2rgba(channels)->save(".sample." + std::to_string(r.camId) + ".png");
				std::cout << "[main] Saved sample image" << std::endl;
			}
		}
	}

	std::cout << "Stopping vision_processor" << std::endl;
	return 0;
}
