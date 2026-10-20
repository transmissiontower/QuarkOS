#ifndef FONT_ATLAS_H
#define FONT_ATLAS_H
#include "stdint.h"
#include "user/libui/gfx.h"

typedef struct {
    uint32_t codepoint;
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

#define FONT_NORMAL 0
#define FONT_BOLD 1
#define FONT_ITALIC 2

void font_atlas_init(uint8_t *raw_data);
int font_atlas_load_from_disk(const char *path);
int icon_font_load_from_disk(const char *path);
void font_atlas_init_for_ptr(AtlasFont *atlas, uint8_t *raw_data);

int utf8_decode(const uint8_t *s, uint32_t *cp);
int draw_codepoint_vector(gfx_context_t *ctx, int x, int y, uint32_t cp, uint32_t color);
int draw_char_vector(gfx_context_t *ctx, int x, int y, char c, uint32_t color);
void draw_string_vector(gfx_context_t *ctx, int x, int y, const char *str, uint32_t color);
int get_codepoint_width(uint32_t cp);
int get_char_width(char c);
int get_string_width(const char *str);
int get_font_height(void);

int draw_codepoint_ex(gfx_context_t *ctx, int x, int y, uint32_t cp, uint32_t color, int size,
                      int flags);
int draw_char_ex(gfx_context_t *ctx, int x, int y, char c, uint32_t color, int size, int flags);
void draw_string_ex(gfx_context_t *ctx, int x, int y, const char *str, uint32_t color, int size,
                    int flags);
int measure_codepoint_ex(uint32_t cp, int size, int flags);
int measure_string_ex(const char *str, int size, int flags);
int get_line_height_ex(int size);

void draw_icon(gfx_context_t *ctx, AtlasFont *atlas, int x, int y, uint32_t icon_id,
               uint32_t color);
void draw_icon_sized(gfx_context_t *ctx, AtlasFont *atlas, int x, int y, uint32_t icon_id,
                     uint32_t color, int size);
#endif
