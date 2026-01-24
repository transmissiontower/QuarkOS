#ifndef STDLIB_H
#define STDLIB_H

void reverse(char *str, int len);
int int_to_str(int x, char str[], int d);
void time_to_str(int x, char *buf);
void ftoa(double n, char *res, int afterpoint);
int strcmp(const char *s1, const char *s2);
int strlen(const char *str);
char *strcpy(char *dest, const char *src);
void bzero(void *dest, int len);
void uint_to_hex(uint32_t val, char *buf);

#endif