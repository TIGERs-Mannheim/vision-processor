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

	for (unsigned int i = 0; i < values.size(); i++) {
		const auto& value = values[i];
		outGroupDiff = std::min(outGroupDiff, (float)(value - contrast).tail<2>().squaredNorm());

		for (unsigned int j = i+1; j < values.size(); j++) {
			inGroupDiff = std::min(inGroupDiff, (float)(values[j] - value).tail<2>().squaredNorm());
		}
	}

	if(inGroupDiff > outGroupDiff)
		return false;

	Eigen::Vector3i c1backup = c1;
	Eigen::Vector3i c2backup = c2;

	//https://reasonabledeviations.com/2019/10/02/k-means-in-cpp/
	//https://www.analyticsvidhya.com/blog/2021/05/k-mean-getting-the-optimal-number-of-clusters/
	c1 = *std::min_element(values.begin(), values.end(), [&](const Eigen::Vector3i& a, const Eigen::Vector3i& b) { return (a - c1).tail<2>().squaredNorm() < (b - c1).tail<2>().squaredNorm(); });
	c2 = *std::min_element(values.begin(), values.end(), [&](const Eigen::Vector3i& a, const Eigen::Vector3i& b) { return (a - c2).tail<2>().squaredNorm() < (b - c2).tail<2>().squaredNorm(); });
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
			if((value - c1).tail<2>().squaredNorm() < (value - c2).tail<2>().squaredNorm()) {
				sum1 += value;
				n1++;
			} else {
				sum2 += value;
				n2++;
			}
		}

		if(n1 == 0 || n2 == 0) {
			c1 = c1backup;
			c2 = c2backup;
			return false;
		}

		oldC1 = c1;
		oldC2 = c2;
		c1 = sum1 / n1;
		c2 = sum2 / n2;
	}

	if((float)(c1 - c2).tail<2>().norm() < sqrtf(outGroupDiff)/2.0f) {
		c1 = c1backup;
		c2 = c2backup;
		return false;
	}

	return true;
}
