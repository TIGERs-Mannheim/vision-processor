#ifndef CL_VERSION_1_0
#include "clstd.h"

#include "image2field.cl"
#define RGGB
#else
#include "kernel/image2field.cl"
#endif


//#define ADD_SQ_COSSIM(pos) vdiff = dx*(float)gx[pos] + dy*(float)gy[pos]; *cossum += fabs(vdiff) / ((float)abs[pos]*native_sqrt((float)dx*dx+dy*dy) + 0.00001);
#define ADD_SQ_COSSIM(pos) vdiff = dx*(float)gx[pos] + dy*(float)gy[pos]; *cossum += vdiff*vdiff / ((float)abs[pos]*(float)abs[pos]*((float)dx*dx+(float)dy*dy) + 0.00001);
inline void px(global const uchar* abs, global const char* gx, global const char* gy, const Perspective perspective, const int xpos, const int ypos, const int dx, const int dy, float* cossum, int* n) {
	const int x = xpos+dx;
	const int y = ypos+dy;
	if(x < 0 || y < 0 || x >= perspective.shape[0]/2 || y >= perspective.shape[1]/2) //TODO is RGGB only
		return;

	const int pos = 2*x + 2*y*perspective.shape[0];
	(*n)++;
	float vdiff;
#ifdef RGGB
	ADD_SQ_COSSIM(pos)
	ADD_SQ_COSSIM(pos+1)
	ADD_SQ_COSSIM(pos+perspective.shape[0])
	ADD_SQ_COSSIM(pos+1+perspective.shape[0])
#endif
}

//https://www.thecrazyprogrammer.com/2016/12/bresenhams-midpoint-circle-algorithm-c-c.html
kernel void ssd(global const uchar* abs, global const char* gx, global const char* gy, global const int* pos, global uchar* out, const Perspective perspective, const float height, const float radius) {
	int xpos = pos[2*get_global_id(0)];
	int ypos = pos[2*get_global_id(0)+1];
#ifdef RGGB
	V2 center = image2field(perspective, height, (V2) {(float)2*xpos, (float)2*ypos});
	V2 offcenter = image2field(perspective, height, (V2) {(float)2*xpos+2, (float)2*ypos});
#endif

	V2 posdiff = {offcenter.x-center.x, offcenter.y-center.y};
	float rPerPixel = native_sqrt(posdiff.x*posdiff.x + posdiff.y*posdiff.y);
	int err = round(radius/rPerPixel);
	int x = err;
	int y = 0;
	err = err - x;

	float cossum = 0.0f;
	int n = 0;
	while(x >= y) {
		px(abs, gx, gy, perspective, xpos, ypos, +x, +y, &cossum, &n);
		px(abs, gx, gy, perspective, xpos, ypos, -y, +x, &cossum, &n);
		px(abs, gx, gy, perspective, xpos, ypos, -x, -y, &cossum, &n);
		px(abs, gx, gy, perspective, xpos, ypos, +y, -x, &cossum, &n);
		if(x > y && y > 0) {
			px(abs, gx, gy, perspective, xpos, ypos, +y, +x, &cossum, &n);
			px(abs, gx, gy, perspective, xpos, ypos, -x, +y, &cossum, &n);
			px(abs, gx, gy, perspective, xpos, ypos, -y, -x, &cossum, &n);
			px(abs, gx, gy, perspective, xpos, ypos, +x, -y, &cossum, &n);
		}

		if(err <= 0) {
			y += 1;
			err += 2*y + 1;
		}
		if(err > 0) {
			x -= 1;
			err += 2*x + 1;
		}
	}

	out[get_global_id(0)] = cossum / n >= 3.0f ? 1 : 0;
}
