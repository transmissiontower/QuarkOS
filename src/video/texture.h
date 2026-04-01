#ifndef TEXTURE_H
#define TEXTURE_H

#include "lib/stdint.h"

typedef struct {
    int width;
    int height;
    uint32_t *data;
} Texture;

extern uint8_t *shadow_corner;
extern uint8_t *shadow_edge;
extern int shadow_size;
extern int shadow_radius;

Texture *create_texture(int w, int h);
void draw_texture(Texture *tex, int x, int y);
void shadows_init(int size, int radius, int intensity);
Texture *rhombus_rounded(int w, int h, int r, uint32_t color);

uint8_t *corner_mask_texture(int r);

#endif