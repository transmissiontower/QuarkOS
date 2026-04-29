// SPDX-License-Identifier: MIT
/*
 * src/lib/math.h - Declarations for floating-point and fixed-point math routines
 */
#ifndef MATH_H
#define MATH_H
#include "lib/stdint.h"
#define PI 3.14159265358979323846
#define PI_FP 205887
#define FP_SHIFT 16
#define FP_ONE (1 << FP_SHIFT)
double sin(double x);
double cos(double x);
double sqrt(double x);
double floor(double x);
double fabs(double x);
double fmod(double x, double y);
int32_t math_fixed_sin(int32_t deg);
int32_t math_fixed_cos(int32_t deg);
#endif
