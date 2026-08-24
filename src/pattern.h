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
#include <eigen3/Eigen/Core>

//1 indicates green, 0 pink, increasing 2d angle starting from bot orientation most significant bit to least significant bit
const int patterns[16] = {
		0b0100, // 0
		0b1100, // 1
		0b1101, // 2
		0b0101, // 3
		0b0010, // 4
		0b1010, // 5
		0b1011, // 6
		0b0011, // 7
		0b1111, // 8
		0b0000, // 9
		0b0110, //10
		0b1001, //11
		0b1110, //12
		0b1000, //13
		0b0111, //14
		0b0001  //15
};
const int patternLUT[16] = { 9, 15, 4, 7, 0, 3, 10, 14, 13, 11, 5, 6, 1, 2, 12, 8 };

const float patternAnglesb2b[25] = {
		 0.        , -2.13940875, -0.56861242,  0.56861242,  2.13940875, // x to center blob
		 1.00218391,  0.        ,  0.21678574,  0.78539816,  1.57079633, // x to top left blob
		 2.57298023, -2.92480691,  0.        ,  1.57079633,  2.35619449, // x to bottom left blob
		-2.57298023, -2.35619449, -1.57079633,  0.        ,  2.92480691, // x to bottom right blob
		-1.00218391, -1.57079633, -0.78539816, -0.21678574,  0.          // x to top right blob
};

const Eigen::Vector2f patternPos[5] = {
		{  0.f   ,   0.f   },
		{ 35.f   ,  54.772f},
		{-54.772f,  35.f   },
		{-54.772f, -35.f   },
		{ 35.f   , -54.772f}
};

const float CENTER_BLOB_RADIUS = 25.0f;
const float SIDE_BLOB_RADIUS = 20.0f;
const float MIN_ROBOT_RADIUS = 85.0f;
const float MIN_ROBOT_FRONT_DISTANCE = 55.0f;
const float MIN_ROBOT_OPENING_ANGLE = 0.86708f;  // 49.68°