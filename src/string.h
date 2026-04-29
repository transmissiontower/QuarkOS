// SPDX-License-Identifier: MIT
/*
 * src/string.h - Standard string/memory function stubs (memset, memcpy, strlen, etc.)
 */
#ifndef _STUB_STRING_H
#define _STUB_STRING_H
#include "lib/stdint.h"
#ifndef NULL
#define NULL ((void *)0)
#endif
typedef unsigned int size_t;
void        *memset(void *s, int c, unsigned int n);
void        *memcpy(void *dest, const void *src, unsigned int n);
int          memcmp(const void *s1, const void *s2, unsigned int n);
void        *memmove(void *dest, const void *src, unsigned int n);
unsigned int strlen(const char *str);
#endif
