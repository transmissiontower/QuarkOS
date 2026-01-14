#include "video/vga.h"
#include "lib/stdint.h"

uint16_t column = 0;
uint16_t line = 0;
uint16_t *const vga = (uint16_t *const)0xB8000;
const uint16_t default_color = (((COLOR8_BLACK << 4) | COLOR8_LIGHT_GREY) << 8);
uint16_t current_color = default_color;

void reset() {
    line = 0;
    column = 0;
    current_color = default_color;

    for (uint16_t y = 0; y < VGA_HEIGHT; y++) {
        for (uint16_t x = 0; x < VGA_WIDTH; x++) {
            vga[y * VGA_WIDTH + x] = ' ' | default_color;
        }
    }
}

void new_line() {
    if (line < VGA_HEIGHT - 1) {
        line++;
        column = 0;
    } else {
        scroll_up();
        column = 0;
    }
}

void scroll_up() {
    for (uint16_t y = 1; y < VGA_HEIGHT; y++) {
        for (uint16_t x = 0; x < VGA_WIDTH; x++) {
            vga[(y - 1) * VGA_WIDTH + x] = vga[y * VGA_WIDTH + x];
        }
    }

    for (uint16_t x = 0; x < VGA_WIDTH; x++) {
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = ' ' | current_color;
    }
}

void print(const char *s) {
    while (*s) {
        switch (*s) {
        case '\n':
            new_line();
            break;
        case '\r':
            column = 0;
            break;
        case '\b':
            if (column == 0 && line != 0) {
                line--;
                column = VGA_WIDTH;
            }
            vga[line * VGA_WIDTH + (--column)] = ' ' | current_color;
            break;
        case '\t':
            if (column == VGA_WIDTH) {
                new_line();
            }
            {
                uint16_t tabLen = 4 - (column % 4);
                while (tabLen--) {
                    vga[line * VGA_WIDTH + (column++)] = ' ' | current_color;
                }
            }
            break;
        default:
            if (column == VGA_WIDTH) {
                new_line();
            }
            vga[line * VGA_WIDTH + (column++)] = *s | current_color;
            break;
        }
        s++;
    }
}

void putc(char c) {
    switch (c) {
    case '\b':
        if (column == 0 && line != 0) {
            line--;
            column = VGA_WIDTH;
        }
        vga[line * VGA_WIDTH + (--column)] = ' ' | current_color;
        break;
    case '\n':
        new_line();
        break;
    case '\r':
        column = 0;
        break;
    case '\t':
        if (column == VGA_WIDTH) {
            new_line();
        }
        {
            uint16_t tabLen = 4 - (column % 4);
            while (tabLen--) {
                vga[line * VGA_WIDTH + (column++)] = ' ' | current_color;
            }
        }
        break;
    default:
        if (column == VGA_WIDTH) {
            new_line();
        }
        vga[line * VGA_WIDTH + (column++)] = c | current_color;
        break;
    }
}
