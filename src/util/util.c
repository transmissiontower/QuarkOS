// SPDX-License-Identifier: MIT
/*
 * src/util/util.c - Memory and color utilities plus VirtIO input compatibility
 */
#include "lib/stdint.h"

static uint8_t ps2_status_latched;
static uint8_t ps2_data_from_mouse_latched;

void memset(void *dest, uint8_t val, uint32_t n) {
    uint8_t *d = dest;
    while (n--)
        *d++ = val;
}
void memcpy(void *dest, const void *src, uint32_t n) {
    char *d = dest;
    const char *s = src;
    while (n--)
        *d++ = *s++;
}
void memcpy32(void *dest, const void *src, uint32_t count) {
    uint32_t *d = (uint32_t *)dest;
    const uint32_t *s = (const uint32_t *)src;
    while (count--)
        *d++ = *s++;
}
void memset32(void *dest, uint32_t val, uint32_t count) {
    uint32_t *d = (uint32_t *)dest;
    while (count--)
        *d++ = val;
}
uint8_t virtio_input_compat_read(uint16_t port) {
    extern void virtio_input_poll(void);
    extern int virtio_input_kbd_has_data(void);
    extern uint8_t virtio_input_kbd_read(void);
    extern int virtio_input_mouse_has_data(void);
    extern uint8_t virtio_input_mouse_read(void);
    if (port == 0x64) {
        virtio_input_poll();

        uint8_t st = 0;
        if (virtio_input_mouse_has_data()) {
            st |= 0x21;
            ps2_data_from_mouse_latched = 1;
        } else if (virtio_input_kbd_has_data()) {
            st |= 0x01;
            ps2_data_from_mouse_latched = 0;
        } else {
            ps2_data_from_mouse_latched = 0;
        }

        ps2_status_latched = st;
        return st;
    }
    if (port == 0x60) {
        uint8_t st = ps2_status_latched;
        ps2_status_latched = 0;

        if ((st & 0x01) != 0) {
            if (ps2_data_from_mouse_latched) {
                if (virtio_input_mouse_has_data())
                    return virtio_input_mouse_read();
            } else {
                if (virtio_input_kbd_has_data())
                    return virtio_input_kbd_read();
            }
        }

        virtio_input_poll();
        if (virtio_input_mouse_has_data())
            return virtio_input_mouse_read();
        if (virtio_input_kbd_has_data())
            return virtio_input_kbd_read();
        return 0;
    }
    return 0;
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
int memcmp(const void *s1, const void *s2, uint32_t n) {
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2)
            return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}
