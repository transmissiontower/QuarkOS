// SPDX-License-Identifier: MIT
/*
 * src/arch/x86/io.h - Inline x86 port I/O helpers (byte, word, and dword)
 */
#ifndef IO_H
#define IO_H
#include "lib/stdint.h"
static inline void io_outb(uint16_t port, uint8_t data)
{
    asm volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}
static inline uint8_t io_inb(uint16_t port)
{
    uint8_t result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}
static inline void io_outw(uint16_t port, uint16_t data)
{
    asm volatile("outw %0, %1" : : "a"(data), "Nd"(port));
}
static inline uint16_t io_inw(uint16_t port)
{
    uint16_t result;
    asm volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}
static inline void io_outl(uint16_t port, uint32_t data)
{
    asm volatile("outl %0, %1" : : "a"(data), "Nd"(port));
}
static inline uint32_t io_inl(uint16_t port)
{
    uint32_t result;
    asm volatile("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}
#endif
