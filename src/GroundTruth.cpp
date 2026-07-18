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
#include "GroundTruth.h"
#include "log.h"

#include <yaml-cpp/yaml.h>


namespace YAML {
	template<> struct [[maybe_unused]] convert<SSL_DetectionBall> {
		[[maybe_unused]] static bool decode(const Node& node, SSL_DetectionBall& ball) {
			ball.set_confidence(node["confidence"].as<float>());
			if(node["area"].IsDefined())
				ball.set_area(node["area"].as<int>());

			ball.set_x(node["x"].as<float>());
			ball.set_y(node["y"].as<float>());
			if(node["z"].IsDefined())
				ball.set_z(node["z"].as<float>());

			ball.set_pixel_x(node["pixel_x"].as<float>());
			ball.set_pixel_y(node["pixel_y"].as<float>());
			return true;
		}
	};

	template<> struct [[maybe_unused]] convert<SSL_DetectionRobot> {
		[[maybe_unused]] static bool decode(const Node& node, SSL_DetectionRobot& robot) {
			robot.set_confidence(node["confidence"].as<float>());
			if(node["robot_id"].IsDefined())
				robot.set_robot_id(node["robot_id"].as<int>());

			robot.set_x(node["x"].as<float>());
			robot.set_y(node["y"].as<float>());
			if(node["orientation"].IsDefined())
				robot.set_orientation(node["orientation"].as<float>());

			robot.set_pixel_x(node["pixel_x"].as<float>());
			robot.set_pixel_y(node["pixel_y"].as<float>());
			if(node["height"].IsDefined())
				robot.set_height(node["height"].as<float>());

			return true;
		}
	};

	template<> struct [[maybe_unused]] convert<SSL_DetectionFrame> {
		[[maybe_unused]] static bool decode(const Node& node, SSL_DetectionFrame& detection) {
			detection.set_camera_id(node["camera_id"].as<int>());
			detection.set_frame_number(node["frame_number"].as<int>());
			detection.set_t_capture(node["t_capture"].as<double>());
			detection.set_t_sent(node["t_sent"].as<double>());
			if(node["t_capture_camera"].IsDefined())
				detection.set_t_capture_camera(node["t_capture_camera"].as<double>());

			for (const auto &item : node["balls"])
				detection.mutable_balls()->Add(item.as<SSL_DetectionBall>());
			for (const auto &item : node["robots_blue"])
				detection.mutable_robots_blue()->Add(item.as<SSL_DetectionRobot>());
			for (const auto &item : node["robots_yellow"])
				detection.mutable_robots_yellow()->Add(item.as<SSL_DetectionRobot>());

			return true;
		}
	};
}

std::vector<SSL_DetectionFrame> parseGroundTruth(const std::string &source) {
	return YAML::LoadFile(source).as<std::vector<SSL_DetectionFrame>>();
}

const SSL_DetectionFrame& getCorrespondingFrame(const std::vector<SSL_DetectionFrame> &groundTruth, uint32_t frameId) {
	for(const auto& frame : groundTruth) {
		if(frame.frame_number() == frameId)
			return frame;
	}

	FATAL("Ground truth for frame not in ground truth data requested. FrameId: " << frameId);
}
