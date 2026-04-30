// SPDX-License-Identifier: MIT
/*
 * src/stdlib.h - Standard library stub providing malloc, free, realloc, and abs
 */
#ifndef _STUB_STDLIB_H
#define _STUB_STDLIB_H
#include "lib/stdint.h"
#ifndef NULL
#define NULL ((void *)0)
#endif
typedef unsigned int size_t;
void *malloc(unsigned int size);
void free(void *ptr);
void *realloc(void *ptr, unsigned int size);
#ifndef abs
#define abs(x) ((x) < 0 ? -(x) : (x))
#endif
#endif
