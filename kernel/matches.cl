#ifndef CL_VERSION_1_0
#include "clstd.h"
#include "image2field.cl"
#else
#include "kernel/image2field.cl"
#endif

typedef struct __attribute__ ((packed)) {
	float2 pos;
	float score;
	float height;
	RGB color;
} Match;

const sampler_t sampler = CLK_FILTER_NEAREST | CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE;

kernel void matches(read_only image2d_t img, read_only image2d_t circ, global Match* matches, global volatile int* counter, const float circThreshold, const uchar sThreshold, const uchar vThreshold, const int radius, const int maxMatches) {
	int2 pos = (int2)(get_global_id(0), get_global_id(1));
	float circScore = read_imagef(circ, sampler, pos).x;
	if(circScore < circThreshold)
		return;

	//TODO subpixel position
	/*if(
			read_imagef(circ, sampler, (int2)(pos.x-1, pos.y)).x > circScore ||
			read_imagef(circ, sampler, (int2)(pos.x+1, pos.y)).x > circScore ||
			read_imagef(circ, sampler, (int2)(pos.x, pos.y-1)).x > circScore ||
			read_imagef(circ, sampler, (int2)(pos.x, pos.y+1)).x > circScore
	)
		return;*/

	int n = 0;
	uint4 color = (uint4)(0, 0, 0, 0);
	{
		const float sqRadius = radius*radius;
		for(int y = pos.y - radius; y <= pos.y + radius; y++) {
			for(int x = pos.x - radius; x <= pos.x + radius; x++) {
				int2 pxpos = (int2)(x, y);
				int2 mpos = pxpos - pos;
				mpos *= mpos;
				if(mpos.x + mpos.y <= sqRadius) {
					color += read_imageui(img, sampler, pxpos);
					n++; //TODO faster by computation
				}
			}
		}
	}

	color /= n;

	int4 stddev;
	{
		const float sqRadius = radius*radius;
		for(int y = pos.y - radius; y <= pos.y + radius; y++) {
			for(int x = pos.x - radius; x <= pos.x + radius; x++) {
				int2 pxpos = (int2)(x, y);
				int2 mpos = pxpos - pos;
				mpos *= mpos;
				if(mpos.x + mpos.y <= sqRadius) {
					int4 s = convert_int4(read_imageui(img, sampler, pxpos)) - convert_int4(color);
					s *= s;
					stddev += s;
					n++;
				}
			}
		}
	}

	uchar3 hsv;
	uchar rgbMin = min(min(color.x, color.y), color.z);
	hsv.z = max(max(color.x, color.y), color.z);
	if (hsv.z == 0) {
		hsv.x = 0;
		hsv.y = 0;
	} else {
		uchar span = hsv.z - rgbMin;
		hsv.y = convert_uchar_sat(255 * convert_int(span) / hsv.z);
		if (hsv.y == 0) {
			hsv.x = 0;
		} else {
			if (hsv.z == color.x)
				hsv.x = 0 + 43 * (color.y - color.z) / span;
			else if (hsv.z == color.y)
				hsv.x = 85 + 43 * (color.z - color.x) / span;
			else
				hsv.x = 171 + 43 * (color.x - color.y) / span;
		}
	}

	//if(hsv.y < sThreshold || hsv.z < vThreshold)
	//	return;

	int i = atomic_inc(counter);
	if(i >= maxMatches)
		return;

	global Match* match = matches + i;
	match->pos = convert_float2(pos);
	match->height = 0;
	match->score = -(stddev.x + stddev.y + stddev.z);
	match->color.r = color.x;
	match->color.g = color.y;
	match->color.b = color.z;
}