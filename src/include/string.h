#ifndef SODIUM_STRING_H
#define SODIUM_STRING_H
#include "stdint.h"
#ifndef NULL
#define NULL ((void *)0)
#endif
#ifndef __SIZE_TYPE__
typedef unsigned int size_t;
#else
typedef __SIZE_TYPE__ size_t;
#endif
void *memset(void *s, int c, unsigned int n);
void *memcpy(void *dest, const void *src, unsigned int n);
int memcmp(const void *s1, const void *s2, unsigned int n);
void *memmove(void *dest, const void *src, unsigned int n);
unsigned int strlen(const char *str);
#endif
