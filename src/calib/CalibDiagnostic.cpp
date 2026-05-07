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
#include "CalibDiagnostic.h"

#include <fstream>
#include <iomanip>
#include <sstream>

static std::string jsonString(const std::string& s) {
	std::string out;
	out.reserve(s.size() + 2);
	out.push_back('"');
	for(char c : s) {
		switch(c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:   out.push_back(c);
		}
	}
	out.push_back('"');
	return out;
}

void CalibDiagnostic::writeJson(const std::string& path) const {
	std::ostringstream o;
	o << std::setprecision(9);
	o << "{\n";
	o << "  \"camera_id\": " << camera_id << ",\n";
	o << "  \"image_width\": " << image_width << ",\n";
	o << "  \"image_height\": " << image_height << ",\n";

	o << "  \"line_corners\": [";
	for(size_t i = 0; i < line_corners.size(); i++) {
		if(i) o << ", ";
		o << "[" << line_corners[i].x() << ", " << line_corners[i].y() << "]";
	}
	o << "],\n";

	o << "  \"camera_height\": " << camera_height << ",\n";
	o << "  \"refinement_enabled\": " << (refinement_enabled ? "true" : "false") << ",\n";

	o << "  \"half_line_width\": " << half_line_width << ",\n";
	o << "  \"line_pixel_count\": " << line_pixel_count << ",\n";
	o << "  \"raw_line_segments\": " << raw_line_segments << ",\n";
	o << "  \"merged_line_count\": " << merged_line_count << ",\n";

	o << "  \"focal_length\": " << focal_length << ",\n";
	o << "  \"position\": [" << position.x() << ", " << position.y() << ", " << position.z() << "],\n";
	o << "  \"euler\": [" << euler.x() << ", " << euler.y() << ", " << euler.z() << "],\n";
	o << "  \"distortion_k2\": " << distortion_k2 << ",\n";
	o << "  \"principal_point\": [" << principal_point.x() << ", " << principal_point.y() << "],\n";

	o << "  \"total_error\": " << total_error << ",\n";
	o << "  \"error_rate\": " << error_rate << ",\n";

	o << "  \"thresholded_image_path\": " << jsonString(thresholded_image_path) << ",\n";
	o << "  \"lines_image_path\": " << jsonString(lines_image_path) << ",\n";
	o << "  \"corner_overlay_path\": " << jsonString(corner_overlay_path) << ",\n";
	o << "  \"refined_overlay_path\": " << jsonString(refined_overlay_path) << "\n";
	o << "}\n";

	std::ofstream f(path);
	f << o.str();
}
