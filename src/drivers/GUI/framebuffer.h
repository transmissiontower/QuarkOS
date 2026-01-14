#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "arch/x86/multiboot.h"
#include "lib/stdint.h"

#define COLOR_BLACK 0x00000000
#define COLOR_WHITE 0x00FFFFFF
#define COLOR_RED 0x00FF0000
#define COLOR_GREEN 0x0000FF00
#define COLOR_BLUE 0x000000FF
#define COLOR_ORANGE 0xEC5800
#define VGA_STATUS 0x3DA

#define RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))

extern uint32_t *fb_ptr;
extern uint8_t *back_buffer;
extern uint32_t fb_width;
extern uint32_t fb_height;
extern uint32_t fb_bpp;
extern uint32_t fb_pitch;

void framebuffer_init(struct multiboot_info *mb_info);

void swap_buffers();
void swap_rect(int x, int y, int w, int h);
void wait_vblank();
void put_pixel(int x, int y, uint32_t color);
void put_pixel_AA(int x, int y, uint32_t color, uint8_t alpha);
uint32_t get_pixel(int x, int y);

void rect_fill(int x, int y, int w, int h, uint32_t color);
void rect_draw(int x, int y, int w, int d, int h, uint32_t color);
void circle_draw_AA(int xc, int yc, int r, uint32_t color);
void circle_fill(int xc, int yc, int r, uint32_t color);
void circle_fill_AA(int xc, int yc, int r, uint32_t color);
void char_draw(int x, int y, char c, uint32_t color, uint32_t bgcolor);
void string_draw(int x, int y, const char *str, uint32_t color, uint32_t bgcolor);
void draw_sprite(int x, int y, int w, int h, const uint8_t *data, const uint32_t *palette);
void draw_corner_mask(int x, int y, int r, uint32_t color, const uint8_t *mask, int quadrant);
void shadow_draw(int x, int y, int w, int h);
void clear_screen(uint32_t color);
void swap_buffers();

#endif