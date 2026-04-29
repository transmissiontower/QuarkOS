// SPDX-License-Identifier: MIT
/*
 * src/video/vga.h - VGA text-mode constants and output function declarations
 */
#ifndef VGA_H
#define VGA_H
#include "lib/stdint.h"
#define COLOR8_BLACK 0
#define COLOR8_LIGHT_GREY 7
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
void print(const char *s);
void putc(char c);
void printf(const char *format, ...);
void scroll_up();
void new_line();
void reset();
#endif
