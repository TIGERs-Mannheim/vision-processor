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

#include <string>
#include <vector>
#include <eigen3/Eigen/Core>

struct CalibDiagnostic {
	int camera_id = -1;
	int image_width = 0;
	int image_height = 0;
	std::vector<Eigen::Vector2f> line_corners;
	double camera_height = 0.0;
	bool refinement_enabled = false;

	int half_line_width = 0;
	int line_pixel_count = 0;
	int raw_line_segments = 0;
	int merged_line_count = 0;

	float focal_length = 0.0f;
	Eigen::Vector3f position = Eigen::Vector3f::Zero();
	Eigen::Vector3f euler = Eigen::Vector3f::Zero();
	float distortion_k2 = 0.0f;
	Eigen::Vector2f principal_point = Eigen::Vector2f::Zero();

	int total_error = 0;
	float error_rate = 0.0f;

	std::string thresholded_image_path;
	std::string lines_image_path;
	std::string corner_overlay_path;
	std::string refined_overlay_path;

	void writeJson(const std::string& path) const;
};
