// SPDX-License-Identifier: MIT
/*
 * src/lib/stdlib.c - Standard string and conversion utilities (strlen, strncpy, itoa, etc.)
 */
#include "lib/stdlib.h"
#include "lib/stdint.h"
void reverse(char *str, int len) {
    int i = 0, j = len - 1;
    while (i < j) {
        char temp = str[i];
        str[i] = str[j];
        str[j] = temp;
        i++;
        j--;
    }
}
int int_to_str(int x, char str[], int d) {
    int i = 0;
    int sign = 0;
    (void)d;
    if (x < 0) {
        x = -x;
        sign = 1;
    }
    while (x) {
        str[i++] = (x % 10) + '0';
        x = x / 10;
    }
    if (i == 0)
        str[i++] = '0';
    if (sign)
        str[i++] = '-';
    reverse(str, i);
    str[i] = '\0';
    return i;
}
void time_to_str(int x, char *buf) {
    if (x < 10) {
        buf[0] = '0';
        int_to_str(x, buf + 1, 0);
    } else {
        int_to_str(x, buf, 0);
    }
}
uint32_t strlen(const char *str) {
    uint32_t len = 0;
    if (!str)
        return 0;
    while (str[len])
        len++;
    return len;
}
char *strncpy(char *dest, const char *src, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}
char *strcat(char *dest, const char *src) {
    char *ptr = dest + strlen(dest);
    while (*src)
        *ptr++ = *src++;
    *ptr = '\0';
    return dest;
}
void bzero(void *dest, int len) {
    char *d = (char *)dest;
    while (len--)
        *d++ = 0;
}
void uint_to_hex(uint32_t val, char *buf) {
    char *chars = "0123456789ABCDEF";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        buf[i + 2] = chars[val & 0xF];
        val >>= 4;
    }
    buf[10] = '\0';
}
char *strstr(const char *haystack, const char *needle) {
    if (!*needle)
        return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack != *needle)
            continue;
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n)
            return (char *)haystack;
    }
    return 0;
}
