#ifndef UGFX_H
#define UGFX_H

#include "lib/stdint.h"

typedef struct {
    uint32_t *buffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
} gfx_context_t;

typedef struct {
    int w, h;
    uint32_t *data;
} Texture;

void gfx_rect_fill(gfx_context_t *ctx, int x, int y, int w, int h, uint32_t color);
void gfx_rect_rounded_fill(gfx_context_t *ctx, int x, int y, int w, int h, int r, uint32_t color);
void gfx_draw_shadow(gfx_context_t *ctx, int x, int y, int w, int h, int r, uint8_t opacity);
void gfx_draw_sprite(gfx_context_t *ctx, int x, int y, int w, int h, const uint8_t *data,
                     const uint32_t *palette);
void gfx_put_pixel_aa(gfx_context_t *ctx, int x, int y, uint32_t color, uint8_t alpha);
uint32_t gfx_blend_colors(uint32_t fg, uint32_t bg, uint8_t alpha);

#endif
