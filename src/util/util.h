#ifndef UTIL_H
#define UTIL_H

#include "lib/stdint.h"

void *memset(void *dest, char val, uint32_t count);
void memset32(void *dest, uint32_t val, uint32_t count);
void memcpy(void *dest, const void *src, uint32_t count);
void memcpy32(void *dest, const void *src, uint32_t count);
void out_port_B(uint16_t port, uint8_t value);
char in_port_B(uint16_t port);
void debug_color(uint32_t color);

int memcmp(const void *s1, const void *s2, uint32_t n);
uint16_t in_port_W(uint16_t port);
uint32_t blend_colors(uint32_t fg, uint32_t bg, uint8_t alpha);
uint32_t isqrt64(uint64_t n);

#define CEIL_DIV(a, b) (((a + b) - 1) / b)

struct InterruptRegisters {
    uint32_t cr2;
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, csm, eflags, useresp, ss;
};

#endif