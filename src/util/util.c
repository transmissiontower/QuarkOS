#include "lib/stdint.h"

void memset(void *dest, uint8_t val, uint32_t n) {
    uint8_t *d = dest;
    while (n--) *d++ = val;
}

void memcpy(void *dest, const void *src, uint32_t n) {
    char *d = dest;
    const char *s = src;
    while (n--) *d++ = *s++;
}

void memcpy32(void *dest, const void *src, uint32_t count) {
    asm volatile ("cld; rep movsl" : "+D"(dest), "+S"(src), "+c"(count) : : "memory");
}

void memset32(void *dest, uint32_t val, uint32_t count) {
    asm volatile ("cld; rep stosl" : "+D"(dest), "+c"(count) : "a"(val) : "memory");
}

void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint32_t blend_colors(uint32_t fg, uint32_t bg, uint8_t alpha) {
    if (alpha == 255)
        return fg;
    if (alpha == 0)
        return bg;

    uint32_t r1 = (fg >> 16) & 0xFF;
    uint32_t g1 = (fg >> 8) & 0xFF;
    uint32_t b1 = fg & 0xFF;

    uint32_t r2 = (bg >> 16) & 0xFF;
    uint32_t g2 = (bg >> 8) & 0xFF;
    uint32_t b2 = bg & 0xFF;

    uint32_t inv_alpha = 255 - alpha;

    uint32_t r = (r1 * alpha + r2 * inv_alpha) >> 8;
    uint32_t g = (g1 * alpha + g2 * inv_alpha) >> 8;
    uint32_t b = (b1 * alpha + b2 * inv_alpha) >> 8;

    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

uint32_t isqrt64(uint64_t n) {
    uint64_t res = 0;
    uint64_t bit = 1ULL << 62;

    while (bit > n) {
        bit >>= 2;
    }

    while (bit != 0) {
        if (n >= res + bit) {
            n -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)res;
}

void debug_color(uint32_t color) {
    uint32_t *vid = (uint32_t *)0xF0000000;
    for (int i = 0; i < 1024 * 100; i++)
        vid[i] = color;
}

int memcmp(const void *s1, const void *s2, uint32_t n) {
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

uint16_t inw(uint16_t port) {
    uint16_t rv;
    asm volatile("inw %1, %0" : "=a"(rv) : "dN"(port));
    return rv;
}

void outw(uint16_t port, uint16_t value) {
    asm volatile("outw %1, %0" : : "dN"(port), "a"(value));
}
