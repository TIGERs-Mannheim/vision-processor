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

#include <memory>
#include "match.h"

class KDTree {
public:
	KDTree(): size(0), data(nullptr) {}
	explicit KDTree(Match* data): data(data) {}
	KDTree(int dim, Match* data): dim(dim), data(data) {}

	void insert(Match* iData);

	void rangeSearch(std::vector<Match*>& values, const Eigen::Vector2f& point, float radius) const;

	[[nodiscard]] inline int getSize() const { return size; }

private:
	std::unique_ptr<KDTree> left = nullptr;
	std::unique_ptr<KDTree> right = nullptr;
	Match* data;

	int dim = 0;
	int size = 1;
};