#include "user/lib/font_atlas.h"
#include "user/lib/libapi.h"

AtlasFont *user_font = 0;
AtlasFont *gui_icons = 0;

void font_atlas_init(uint8_t *raw_data)
{
    if (!raw_data || *(uint32_t *)raw_data != 0x464F4E54)
        return;
    user_font = (AtlasFont *)malloc(sizeof(AtlasFont));
    user_font->tex_width = *(uint32_t *)(raw_data + 4);
    user_font->tex_height = *(uint32_t *)(raw_data + 8);
    user_font->line_height = *(uint32_t *)(raw_data + 12);
    user_font->ascent = *(uint32_t *)(raw_data + 16);
    user_font->count = *(uint32_t *)(raw_data + 20);
    user_font->glyphs = (GlyphInfo *)(raw_data + 24);
    user_font->bitmap = (uint32_t *)(raw_data + 24 + user_font->count * sizeof(GlyphInfo));
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

static GlyphInfo *glyph_find_in(AtlasFont *font, uint32_t cp)
{
    if (!font) return 0;
    int lo = 0, hi = (int)font->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t mcp = font->glyphs[mid].codepoint;
        if (mcp == cp) return &font->glyphs[mid];
        if (mcp < cp)  lo = mid + 1;
        else           hi = mid - 1;
    }
    return 0;
}

int get_codepoint_width(uint32_t cp)
{
    if (!user_font) return 0;
    GlyphInfo *g = glyph_find_in(user_font, cp);
    return g ? g->advance : (int)user_font->line_height / 2;
}

int get_char_width(char c)
{
    return get_codepoint_width((uint32_t)(uint8_t)c);
}

int get_font_height()
{
    return user_font ? user_font->line_height : 0;
}

int draw_codepoint_vector(gfx_context_t *ctx, int x, int y, uint32_t cp, uint32_t color)
{
    if (!user_font) return x;
    GlyphInfo *g = glyph_find_in(user_font, cp);
    if (!g) return x + (int)user_font->line_height / 2;
    if (g->w == 0) return x + g->advance;

    int top_y = y + user_font->ascent + g->off_y;

    for (int iy = 0; iy < g->h; iy++) {
        for (int ix = 0; ix < g->w; ix++) {
            uint32_t px = user_font->bitmap[(g->y + iy) * user_font->tex_width + (g->x + ix)];
            uint8_t a = (px >> 24) & 0xFF;
            if (a > 0) {
                gfx_put_pixel_aa(ctx, x + g->off_x + ix, top_y + iy, color, a);
            }
        }
    }
    return x + g->advance;
}

int draw_char_vector(gfx_context_t *ctx, int x, int y, char c, uint32_t color)
{
    return draw_codepoint_vector(ctx, x, y, (uint32_t)(uint8_t)c, color);
}

void draw_string_vector(gfx_context_t *ctx, int x, int y, const char *str, uint32_t color)
{
    if (!user_font || !str)
        return;
    int start_x = x;
    const uint8_t *p = (const uint8_t *)str;
    while (*p) {
        if (*p == '\n') {
            y += user_font->line_height;
            x = start_x;
            p++;
        } else {
            uint32_t cp;
            int n = utf8_decode(p, &cp);
            x = draw_codepoint_vector(ctx, x, y, cp, color);
            p += n;
        }
    }
}

void draw_icon(gfx_context_t *ctx, AtlasFont *atlas, int x, int y, uint32_t icon_id, uint32_t color)
{
    if (!atlas || icon_id < 32)
        return;

    GlyphInfo *g = glyph_find_in(atlas, icon_id);
    if (!g) return;

    for (int iy = 0; iy < g->h; iy++) {
        for (int ix = 0; ix < g->w; ix++) {
            uint32_t px = atlas->bitmap[(g->y + iy) * atlas->tex_width + (g->x + ix)];
            uint8_t a = (px >> 24) & 0xFF;

            if (a > 0) {
                gfx_put_pixel_aa(ctx, x + g->off_x + ix, y + g->off_y + iy, color, a);
            }
        }
    }
}

void draw_icon_sized(gfx_context_t *ctx, AtlasFont *atlas, int x, int y,
                     uint32_t icon_id, uint32_t color, int size)
{
    if (!atlas || icon_id < 32 || size <= 0)
        return;
    GlyphInfo *g = glyph_find_in(atlas, icon_id);
    if (!g || g->w == 0 || g->h == 0) return;

    
    for (int oy = 0; oy < size; oy++) {
        int sy0 = oy * g->h / size;
        int sy1 = (oy + 1) * g->h / size;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int ox = 0; ox < size; ox++) {
            int sx0 = ox * g->w / size;
            int sx1 = (ox + 1) * g->w / size;
            if (sx1 <= sx0) sx1 = sx0 + 1;

            uint32_t sum = 0, count = 0;
            for (int sy = sy0; sy < sy1; sy++) {
                for (int sx = sx0; sx < sx1; sx++) {
                    uint32_t px = atlas->bitmap[(g->y + sy) * atlas->tex_width + (g->x + sx)];
                    sum += (px >> 24) & 0xFF;
                    count++;
                }
            }
            uint8_t a = (uint8_t)(sum / count);
            if (a > 0)
                gfx_put_pixel_aa(ctx, x + ox, y + oy, color, a);
        }
    }
}

void font_atlas_init_for_ptr(AtlasFont *atlas, uint8_t *raw_data)
{
    if (!raw_data || !atlas || *(uint32_t *)raw_data != 0x464F4E54)
        return;
    atlas->tex_width = *(uint32_t *)(raw_data + 4);
    atlas->tex_height = *(uint32_t *)(raw_data + 8);
    atlas->line_height = *(uint32_t *)(raw_data + 12);
    atlas->ascent = *(uint32_t *)(raw_data + 16);
    atlas->count = *(uint32_t *)(raw_data + 20);
    atlas->glyphs = (GlyphInfo *)(raw_data + 24);
    atlas->bitmap = (uint32_t *)(raw_data + 24 + atlas->count * sizeof(GlyphInfo));
}
