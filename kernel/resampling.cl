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
	int shape[2];  // raw image shape
	float f;       // 1/(focal length)
	float p[2];    // principal point
	float d;       // distortion
	float r[9];    // rotation matrix
	float c[3];    // camera position
} CameraModel;

inline float2 field2image(const CameraModel m, float3 fieldpos) {
	fieldpos -= (float3)(m.c[0], m.c[1], m.c[2]);

	float3 camRay = (float3)(
			m.r[0] * fieldpos.x + m.r[1] * fieldpos.y + m.r[2] * fieldpos.z,
			m.r[3] * fieldpos.x + m.r[4] * fieldpos.y + m.r[5] * fieldpos.z,
			m.r[6] * fieldpos.x + m.r[7] * fieldpos.y + m.r[8] * fieldpos.z
	);
	float2 camRay2 = (float2)(camRay.x / camRay.z, camRay.y / camRay.z);

	float2 camRayU = camRay2;
	for(int i = 0; i < 8; i++) {
		float2 r = camRayU*camRayU;
		float dr = 1 + m.d*(r.x + r.y);
		camRayU = camRay2/dr;
	}

	return m.f * camRayU + (float2)(m.p[0], m.p[1]);
}


const sampler_t sampler = CLK_FILTER_LINEAR | CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE;

kernel void resampling(read_only image2d_t channel0, read_only image2d_t channel1, read_only image2d_t channel2, read_only image2d_t channel3, write_only image2d_t out, const CameraModel model, const float maxRobotHeight, const float fieldScale, const float fieldOffsetX, const float fieldOffsetY) {
	float2 pos = field2image(model, (float3)(get_global_id(0)*fieldScale + fieldOffsetX, get_global_id(1)*fieldScale + fieldOffsetY, maxRobotHeight));

#ifdef BGR
	uint4 color = (uint4)(
			read_imageui(channel2, sampler, pos).x,
			read_imageui(channel1, sampler, pos).x,
			read_imageui(channel0, sampler, pos).x,
			255
	);
#endif

#ifdef RGGB
	uint4 color = (uint4)(
			read_imageui(channel0, sampler, (float2)(pos.x + 0.25f, pos.y + 0.25f)).x,
			read_imageui(channel1, sampler, (float2)(pos.x - 0.25f, pos.y + 0.25f)).x/2 + read_imageui(channel2, sampler, (float2)(pos.x + 0.25f, pos.y - 0.25f)).x/2,
			read_imageui(channel3, sampler, (float2)(pos.x - 0.25f, pos.y - 0.25f)).x,
			255
	);
#endif

#ifdef GRBG
	uint4 color = (uint4)(
			read_imageui(channel1, sampler, (float2)(pos.x - 0.25f, pos.y + 0.25f)).x,
			read_imageui(channel0, sampler, (float2)(pos.x + 0.25f, pos.y + 0.25f)).x/2 + read_imageui(channel3, sampler, (float2)(pos.x - 0.25f, pos.y - 0.25f)).x/2,
			read_imageui(channel2, sampler, (float2)(pos.x + 0.25f, pos.y - 0.25f)).x,
			255
	);
#endif

	// RGB
	//write_imageui(out, (int2)(get_global_id(0), get_global_id(1)), color);

	// dRGB
	write_imageui(out, (int2)(get_global_id(0), get_global_id(1)), (uint4)(
			(2*color.r - color.g - color.b + 510) / 4,
			(2*color.g - color.b - color.r + 510) / 4,
			(2*color.b - color.r - color.g + 510) / 4,
			255
	));

	// YUV
	/*write_imageui(out, (int2)(get_global_id(0), get_global_id(1)), (uint4)(
			128,
			convert_uchar_sat((-38*(int)color.r + -74*(int)color.g + 112*(int)color.b) / 256 + 128),
			convert_uchar_sat((112*(int)color.r + -94*(int)color.g + -18*(int)color.b) / 256 + 128),
			255
	));*/
}