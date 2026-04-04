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

int get_char_width(char c)
{
    if (!user_font || c < 32 || c > 126)
        return 0;
    return user_font->glyphs[c - 32].advance;
}

int get_font_height()
{
    return user_font ? user_font->line_height : 0;
}

int draw_char_vector(gfx_context_t *ctx, int x, int y, char c, uint32_t color)
{
    if (!user_font || c < 32 || c > 126)
        return x;
    GlyphInfo *g = &user_font->glyphs[c - 32];

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

void draw_string_vector(gfx_context_t *ctx, int x, int y, const char *str, uint32_t color)
{
    if (!user_font || !str)
        return;
    int cur_x = x;
    while (*str) {
        cur_x = draw_char_vector(ctx, cur_x, y, *str++, color);
    }
}

void draw_icon(gfx_context_t *ctx, AtlasFont *atlas, int x, int y, uint32_t icon_id, uint32_t color)
{
    if (!atlas || icon_id < 32)
        return;

    GlyphInfo *g = &atlas->glyphs[icon_id - 32];

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
