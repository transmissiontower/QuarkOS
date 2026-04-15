#include "gfx/font_atlas.h"
#include "drivers/GUI/framebuffer.h"
#include "mm/kmalloc.h"
#include "util/util.h"
AtlasFont *main_font = 0;
void font_atlas_init(uint8_t *raw_data)
{
    uint32_t *header = (uint32_t *)raw_data;
    if (header[0] != 0x464F4E54) {
        return;
    }
    main_font = (AtlasFont *)kmalloc(sizeof(AtlasFont));
    main_font->tex_width = header[1];
    main_font->tex_height = header[2];
    main_font->line_height = header[3];
    main_font->ascent = header[4];
    main_font->count = header[5];
    main_font->glyphs = (GlyphInfo *)(raw_data + 24);
    uint32_t metrics_size = main_font->count * sizeof(GlyphInfo);
    main_font->bitmap = (uint32_t *)(raw_data + 24 + metrics_size);
}
int utf8_decode(const uint8_t *s, uint32_t *cp)
{
    uint8_t b = s[0];
    if (b < 0x80) { *cp = b; return 1; }
    if ((b & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(b & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    }
    if ((b & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    }
    if ((b & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(b & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
              ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        return 4;
    }
    *cp = 0xFFFD; return 1;
}
static GlyphInfo *glyph_find(uint32_t cp)
{
    if (!main_font) return 0;
    int lo = 0, hi = (int)main_font->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t mcp = main_font->glyphs[mid].codepoint;
        if (mcp == cp) return &main_font->glyphs[mid];
        if (mcp < cp)  lo = mid + 1;
        else           hi = mid - 1;
    }
    return 0;
}
int draw_codepoint_vector(int x, int y, uint32_t cp, uint32_t color)
{
    if (!main_font) return x;
    GlyphInfo *g = glyph_find(cp);
    if (!g) return x + (int)main_font->line_height / 2;
    if (g->w == 0) return x + g->advance;
    int screen_start_y = y + g->off_y;
    int screen_start_x = x + g->off_x;
    for (int iy = 0; iy < g->h; iy++) {
        for (int ix = 0; ix < g->w; ix++) {
            int tex_x = g->x + ix;
            int tex_y = g->y + iy;
            uint32_t src = main_font->bitmap[tex_y * main_font->tex_width + tex_x];
            uint8_t alpha = (src >> 24) & 0xFF;
            if (alpha > 0) {
                put_pixel_AA(screen_start_x + ix, screen_start_y + iy, color, alpha);
            }
        }
    }
    return x + g->advance;
}
int draw_char_vector(int x, int y, char c, uint32_t color)
{
    return draw_codepoint_vector(x, y, (uint32_t)(uint8_t)c, color);
}
int get_codepoint_width(uint32_t cp)
{
    if (!main_font) return 0;
    GlyphInfo *g = glyph_find(cp);
    return g ? g->advance : (int)main_font->line_height / 2;
}
int get_char_width(char c)
{
    return get_codepoint_width((uint32_t)(uint8_t)c);
}
int get_font_height()
{
    if (!main_font)
        return 0;
    return main_font->line_height;
}
void draw_string_vector(int x, int y, const char *str, uint32_t color)
{
    if (!main_font || !str)
        return;
    int start_x = x;
    const uint8_t *p = (const uint8_t *)str;
    while (*p) {
        if (*p == '\n') {
            y += main_font->line_height;
            x = start_x;
            p++;
        } else {
            uint32_t cp;
            int n = utf8_decode(p, &cp);
            x = draw_codepoint_vector(x, y, cp, color);
            p += n;
        }
    }
}
int get_string_width(const char *str)
{
    if (!main_font || !str)
        return 0;
    int w = 0;
    const uint8_t *p = (const uint8_t *)str;
    while (*p) {
        uint32_t cp;
        int n = utf8_decode(p, &cp);
        w += get_codepoint_width(cp);
        p += n;
    }
    return w;
}
