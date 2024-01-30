#pragma once

#define kernel
#define global
#define local
#define constant

typedef unsigned char uchar;

int get_global_id(int);
int get_global_size(int);

int min(int, int);
int max(int, int);
bool isnan(float);

float native_sqrt(float);
uchar convert_uchar_sat(float);
float fabs(float);

#define INFINITY 9999999999999.9f
#define NAN 9999999999999.9f
