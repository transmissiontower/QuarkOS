#ifndef SODIUM_STDLIB_H
#define SODIUM_STDLIB_H
#include "stdint.h"
#ifndef NULL
#define NULL ((void *)0)
#endif
typedef __SIZE_TYPE__ size_t;
void *malloc(unsigned int size);
void free(void *ptr);
void *realloc(void *ptr, unsigned int size);
#ifndef abs
#define abs(x) ((x) < 0 ? -(x) : (x))
#endif
void reverse(char *str, int len);
int int_to_str(int x, char str[], int d);
void time_to_str(int x, char *buf);
void ftoa(double n, char *res, int afterpoint);
int strcmp(const char *s1, const char *s2);
uint32_t strlen(const char *str);
char *strncpy(char *dest, const char *src, uint32_t n);
void bzero(void *dest, int len);
void uint_to_hex(uint32_t val, char *buf);
char *strstr(const char *haystack, const char *needle);
char *strcat(char *dest, const char *src);

#endif
