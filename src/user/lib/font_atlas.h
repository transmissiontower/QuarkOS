#ifndef FONT_ATLAS_H
#define FONT_ATLAS_H

#include "lib/stdint.h"
#include "user/lib/gfx.h"

typedef struct {
    uint16_t x, y, w, h;
    int16_t advance, off_x, off_y;
} __attribute__((packed)) GlyphInfo;

typedef struct {
    uint32_t tex_width, tex_height, line_height, ascent, count;
    GlyphInfo *glyphs;
    uint32_t *bitmap;
} AtlasFont;

extern AtlasFont *user_font;
extern AtlasFont *gui_icons;

void font_atlas_init(uint8_t *raw_data);
void draw_string_vector(gfx_context_t *ctx, int x, int y, const char *str, uint32_t color);
int get_char_width(char c);
int get_font_height();

void draw_icon(gfx_context_t *ctx, AtlasFont *atlas, int x, int y, uint32_t icon_id,
               uint32_t color);
void font_atlas_init_for_ptr(AtlasFont *atlas, uint8_t *raw_data);

#endif