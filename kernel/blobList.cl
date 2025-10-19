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
#ifndef CL_VERSION_1_0
#include "clstd.h"
#endif

typedef struct __attribute__ ((packed)) {
	uchar r;
	uchar g;
	uchar b;
} RGB;

typedef struct __attribute__ ((packed)) {
	float x, y;
	RGB color;
	RGB center;
	float circ;
	float score;
} Match;

const sampler_t sampler = CLK_FILTER_NEAREST | CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE;

kernel void matches(read_only image2d_t img, read_only image2d_t circ, global Match* matches, global volatile int* counter, const float circThreshold, const float minScore, const int radius, const int maxMatches) {
	const int2 pos = (int2)(get_global_id(0), get_global_id(1));
	float circScore = read_imagef(circ, sampler, pos).x;
	if(circScore < circThreshold)
		return;

	// Filter to only local peaks
	float circNegX = read_imagef(circ, sampler, (int2)(pos.x-1, pos.y)).x;
	float circPosX = read_imagef(circ, sampler, (int2)(pos.x+1, pos.y)).x;
	float circNegY = read_imagef(circ, sampler, (int2)(pos.x, pos.y-1)).x;
	float circPosY = read_imagef(circ, sampler, (int2)(pos.x, pos.y+1)).x;
	if(
			circNegX > circScore ||
			circPosX > circScore ||
			circNegY > circScore ||
			circPosY > circScore
	) {
		atomic_inc(counter+2);
		return;
	}

	//https://en.wikipedia.org/wiki/Standard_deviation#Rapid_calculation_methods
	int n = 0;
	uint4 s1 = (uint4)(0, 0, 0, 0);
	uint4 s2 = (uint4)(0, 0, 0, 0); // Value estimation (255*255) * (16*16) /256^4 (far in range of uint)
	{
		const int sqRadius = radius*radius;
		for(int y = -radius; y <= radius; y++) {
			for(int x = -radius; x <= radius; x++) {
				if(x*x + y*y <= sqRadius) {
					uint4 v = read_imageui(img, sampler, pos + (int2)(x, y));
					s1 += v;
					s2 += v*v;
					n++; //TODO faster by computation? -> https://mathworld.wolfram.com/GausssCircleProblem.html
				}
			}
		}
	}

	//https://en.wikipedia.org/wiki/Summed-area_table
	float4 stddev = native_sqrt((convert_float4(s2) - convert_float4(s1)*convert_float4(s1)/n) / n);

	float score = circScore / (stddev.y + stddev.z);
	if(score < minScore) {
		atomic_inc(counter+1);
		return;
	}

	uint4 center = read_imageui(img, sampler, pos);
	uint4 color = s1 / n;

	int i = atomic_inc(counter);
	if(i >= maxMatches)
		return;

	global Match* match = matches + i;
	//https://ccrma.stanford.edu/~jos/sasp/Quadratic_Interpolation_Spectral_Peaks.html
	match->x = pos.x + 0.5f * (circNegX - circPosX) / (circNegX - 2*circScore + circPosX);
	match->y = pos.y + 0.5f * (circNegY - circPosY) / (circNegY - 2*circScore + circPosY);
	match->color.r = color.r;
	match->color.g = color.g;
	match->color.b = color.b;
	match->center.r = center.r;
	match->center.g = center.g;
	match->center.b = center.b;
	match->circ = circScore;
	match->score = score;
}