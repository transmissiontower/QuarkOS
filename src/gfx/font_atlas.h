#ifndef FONT_ATLAS_H
#define FONT_ATLAS_H

#include "lib/stdint.h"

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    int16_t advance;
    int16_t off_x;
    int16_t off_y;
} __attribute__((packed)) GlyphInfo;

typedef struct {
    uint32_t tex_width;
    uint32_t tex_height;
    uint32_t line_height;
    uint32_t ascent;
    uint32_t count;
    GlyphInfo *glyphs;
    uint32_t *bitmap;
} AtlasFont;

extern AtlasFont *main_font;

void font_atlas_init(uint8_t *rawData);
int draw_char_vector(int x, int y, char c, uint32_t color);
void draw_string_vector(int x, int y, const char *str, uint32_t color);
int get_string_width(const char *str);
int get_char_width(char c);
int get_font_height();

#endif