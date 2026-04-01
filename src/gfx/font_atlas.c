#include "gfx/font_atlas.h"
#include "drivers/GUI/framebuffer.h"
#include "mm/kmalloc.h"
#include "util/util.h"

AtlasFont *main_font = 0;

void font_atlas_init(uint8_t *raw_data) {
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

int draw_char_vector(int x, int y, char c, uint32_t color) {
    if (!main_font || c < 32 || c > 126)
        return x;

    int idx = c - 32;
    GlyphInfo *g = &main_font->glyphs[idx];

    if (g->w == 0)
        return x + g->advance;

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

int get_char_width(char c) {
    if (!main_font || c < 32 || c > 126)
        return 0;
    return main_font->glyphs[c - 32].advance;
}

int get_font_height() {
    if (!main_font)
        return 0;
    return main_font->line_height;
}

void draw_string_vector(int x, int y, const char *str, uint32_t color) {
    if (!main_font)
        return;

    int start_x = x;
    int current_y = y;

    while (*str) {
        char c = *str;

        if (c == '\n') {
            current_y += main_font->line_height;
            x = start_x;
        } else {
            x = draw_char_vector(x, current_y, c, color);
        }
        str++;
    }
}

int get_string_width(const char *str) {
    if (!main_font)
        return 0;
    int w = 0;
    while (*str) {
        char c = *str;
        if (c >= 32 && c <= 126) {
            w += main_font->glyphs[c - 32].advance;
        }
        str++;
    }
    return w;
}