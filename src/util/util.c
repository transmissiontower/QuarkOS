#include "lib/math.h"
#include "lib/stdint.h"

void *memset(void *dest, char val, uint32_t count) {
    char *temp = (char *)dest;
    for (; count != 0; count--) {
        *temp++ = val;
    }
    return dest;
}

void memset32(void *dest, uint32_t val, uint32_t count) {
    uint32_t *temp = (uint32_t *)dest;

    while (count >= 8) {
        temp[0] = val;
        temp[1] = val;
        temp[2] = val;
        temp[3] = val;
        temp[4] = val;
        temp[5] = val;
        temp[6] = val;
        temp[7] = val;
        temp += 8;
        count -= 8;
    }
    while (count--) {
        *temp++ = val;
    }
}

void memcpy(void *dest, const void *src, uint32_t count) {
    const char *sp = (const char *)src;
    char *dp = (char *)dest;
    for (; count != 0; count--)
        *dp++ = *sp++;
}

void memcpy32(void *dest, const void *src, uint32_t count) {
    uint32_t *dp = (uint32_t *)dest;
    const uint32_t *sp = (const uint32_t *)src;

    uint32_t dwords = count >> 2;
    uint32_t bytes = count & 3;

    while (dwords >= 8) {
        dp[0] = sp[0];
        dp[1] = sp[1];
        dp[2] = sp[2];
        dp[3] = sp[3];
        dp[4] = sp[4];
        dp[5] = sp[5];
        dp[6] = sp[6];
        dp[7] = sp[7];
        dp += 8;
        sp += 8;
        dwords -= 8;
    }

    while (dwords--) {
        *dp++ = *sp++;
    }

    uint8_t *db = (uint8_t *)dp;
    const uint8_t *sb = (const uint8_t *)sp;
    while (bytes--) {
        *db++ = *sb++;
    }
}

void out_port_B(uint16_t port, uint8_t value) {
    asm volatile("outb %1, %0" : : "dN"(port), "a"(value));
}

char in_port_B(uint16_t port) {
    char rv;
    asm volatile("inb %1, %0" : "=a"(rv) : "dN"(port));
    return rv;
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

uint16_t in_port_W(uint16_t port) {
    uint16_t rv;
    asm volatile("inw %1, %0" : "=a"(rv) : "dN"(port));
    return rv;
}