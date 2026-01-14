#ifndef MATH_H
#define MATH_H

#include "lib/stdint.h"

#define PI 3.14159265358979323846
#define PI_FP 205887
#define FP_SHIFT 16
#define FP_ONE (1 << FP_SHIFT)

double sin(double x);
int32_t i_sin(int32_t x);
double cos(double x);
int32_t i_cos(int32_t x);
double sqrt(double x);
double floor(double x);
double fabs(double x);
double fmod(double x, double y);

int32_t deg_to_rad(int32_t deg);

#endif
