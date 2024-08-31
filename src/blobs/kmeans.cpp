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
#include "kmeans.h"
#include <algorithm>
#include <cmath>

bool kMeans(const Eigen::Vector3i& contrast, const std::vector<Eigen::Vector3i>& values, Eigen::Vector3i& c1, Eigen::Vector3i& c2) {
	if(values.size() < 2)
		return false;

	float inGroupDiff = INFINITY;
	float outGroupDiff = INFINITY;

	for (int i = 0; i < values.size(); i++) {
		const auto& value = values[i];
		outGroupDiff = std::min(outGroupDiff, (float)(value - contrast).squaredNorm());

		for (int j = i+1; j < values.size(); j++) {
			inGroupDiff = std::min(inGroupDiff, (float)(values[j] - value).squaredNorm());
		}
	}

	if(inGroupDiff > outGroupDiff) {
		//std::cerr << "   Ingroup bigger than outgroup" << std::endl;
		return false;
	}

	inGroupDiff = sqrtf(inGroupDiff);
	outGroupDiff = sqrtf(outGroupDiff);

	Eigen::Vector3i c1backup = c1;
	Eigen::Vector3i c2backup = c2;

	//https://reasonabledeviations.com/2019/10/02/k-means-in-cpp/
	//https://www.analyticsvidhya.com/blog/2021/05/k-mean-getting-the-optimal-number-of-clusters/
	c1 = *std::min_element(values.begin(), values.end(), [&](const Eigen::Vector3i& a, const Eigen::Vector3i& b) { return (a - c1).squaredNorm() < (b - c1).squaredNorm(); });
	c2 = *std::min_element(values.begin(), values.end(), [&](const Eigen::Vector3i& a, const Eigen::Vector3i& b) { return (a - c2).squaredNorm() < (b - c2).squaredNorm(); });
	if(c1 == c2) {
		c1 = c1backup;
		c2 = c2backup;
		return false;
	}

	Eigen::Vector3i oldC1 = c2;
	Eigen::Vector3i oldC2 = c1;
	int n1 = 0;
	int n2 = 0;
	while(oldC1 != c1 && oldC2 != c2) {
		Eigen::Vector3i sum1 = {0, 0, 0};
		Eigen::Vector3i sum2 = {0, 0, 0};
		n1 = 0;
		n2 = 0;
		for (const auto& value : values) {
			if((value - c1).squaredNorm() < (value - c2).squaredNorm()) {
				sum1 += value;
				n1++;
			} else {
				sum2 += value;
				n2++;
			}
		}

		if(n1 == 0 || n2 == 0) {
			//std::cerr << "   N0 " << n1 << "|" << n2 << "   " << c1backup.transpose() << "|" << c2backup.transpose() << "   " << c1.transpose() << "|" << c2.transpose() << std::endl;
			c1 = c1backup;
			c2 = c2backup;
			return false;
		}

		oldC1 = c1;
		oldC2 = c2;
		c1 = sum1 / n1;
		c2 = sum2 / n2;
	}

	float mergeRange = (outGroupDiff - inGroupDiff) / 2.0f;
	if((float)(c1 - c2).norm() < mergeRange) {
		//std::cerr << "   Skipping Update for " << c1backup.transpose() << "|" << c2backup.transpose() << "   " << c1.transpose() << "|" << c2.transpose() << std::endl;
		c1 = c1backup;
		c2 = c2backup;
		return false;
	}

	/*if((c1 - c2).dot(c1backup - c2backup) <= 0) { //TODO did never trigger
		std::cerr << "   Attempted color direction inversion" << std::endl;
		c1 = c1backup;
		c2 = c2backup;
	}*/

	// https://en.wikipedia.org/wiki/Silhouette_(clustering)#Simplified_Silhouette_and_Medoid_Silhouette
	/*float s1 = 0.0;
	float s2 = 0.0;
	for (const auto& value : values) {
		float a = (float)(value - c1).norm();
		float b = (float)(value - c2).norm();
		if(a < b) {
			s1 += (b - a) / b;
		} else {
			s2 += (a - b) / a;
		}
	}

	//TODO circularity of samples: if roughly circular: one cluster?
	//TODO if small sample size: combine multiple frames
	if(std::max(s1/(float)n1, s2/(float)n2) < 1.0f) { //TODO not working	 //TODO hardcoded value
		std::cerr << "   Skipping Update for " << n1 << "|" << n2 << "   " << c1backup.transpose() << "|" << c2backup.transpose() << "   " << c1.transpose() << "|" << c2.transpose() << std::endl;
		c1 = c1backup;
		c2 = c2backup;
	}*/

	return true;
}
