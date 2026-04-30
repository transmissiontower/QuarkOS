// SPDX-License-Identifier: MIT
/*
 * src/gfx/font_atlas.c - Font atlas loader, glyph renderer, and TTF rasterizer
 */
#include "gfx/font_atlas.h"
#include "drivers/GUI/framebuffer.h"
#include "fs/fat.h"
#include "lib/math.h"
#include "mm/kmalloc.h"
#include "util/util.h"

AtlasFont *main_font = 0;

void font_atlas_init(uint8_t *raw_data) {
    uint32_t *header = (uint32_t *)raw_data;
    if (header[0] != 0x464F4E54) {
        return;
    }
    main_font = (AtlasFont *)mm_heap_alloc(sizeof(AtlasFont));
    main_font->tex_width = header[1];
    main_font->tex_height = header[2];
    main_font->line_height = header[3];
    main_font->ascent = header[4];
    main_font->count = header[5];
    main_font->glyphs = (GlyphInfo *)(raw_data + 24);
    uint32_t metrics_size = main_font->count * sizeof(GlyphInfo);
    main_font->bitmap = (uint32_t *)(raw_data + 24 + metrics_size);
}

int utf8_decode(const uint8_t *s, uint32_t *cp) {
    uint8_t b = s[0];
    if (b < 0x80) {
        *cp = b;
        return 1;
    }
    if ((b & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(b & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    }
    if ((b & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    }
    if ((b & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
        (s[3] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(b & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
              ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        return 4;
    }
    *cp = 0xFFFD;
    return 1;
}

static GlyphInfo *glyph_find(uint32_t cp) {
    if (!main_font)
        return 0;
    int lo = 0, hi = (int)main_font->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t mcp = main_font->glyphs[mid].codepoint;
        if (mcp == cp)
            return &main_font->glyphs[mid];
        if (mcp < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

int draw_codepoint_vector(int x, int y, uint32_t cp, uint32_t color) {
    if (!main_font)
        return x;
    GlyphInfo *g = glyph_find(cp);
    if (!g)
        return x + (int)main_font->line_height / 2;
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

int draw_char_vector(int x, int y, char c, uint32_t color) {
    return draw_codepoint_vector(x, y, (uint32_t)(uint8_t)c, color);
}

int get_codepoint_width(uint32_t cp) {
    if (!main_font)
        return 0;
    GlyphInfo *g = glyph_find(cp);
    return g ? g->advance : (int)main_font->line_height / 2;
}

int get_char_width(char c) {
    return get_codepoint_width((uint32_t)(uint8_t)c);
}

int get_font_height() {
    if (!main_font)
        return 0;
    return main_font->line_height;
}

void draw_string_vector(int x, int y, const char *str, uint32_t color) {
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

int get_string_width(const char *str) {
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

static double stb_ceil_k(double x) {
    int i = (int)x;
    return (double)i + (x > (double)i ? 1.0 : 0.0);
}

static double stb_acos_k(double x) {
    return 1.5707963 - (x + x * x * x / 6.0 + x * x * x * x * x * 3.0 / 40.0);
}

#ifndef _STUB_STRING_H
#define _STUB_STRING_H
#endif
typedef unsigned int size_t;

#define NULL 0
#define STBTT_ifloor(x) ((int)floor((double)(x)))
#define STBTT_iceil(x) ((int)stb_ceil_k((double)(x)))
#define STBTT_sqrt(x) sqrt((double)(x))
#define STBTT_pow(x, y) sqrt((double)(x))
#define STBTT_fmod(x, y) fmod((double)(x), (double)(y))
#define STBTT_cos(x) cos((double)(x))
#define STBTT_acos(x) stb_acos_k((double)(x))
#define STBTT_fabs(x) fabs((double)(x))
#define STBTT_assert(x) ((void)0)
#define STBTT_malloc(x, u) mm_heap_alloc(x)
#define STBTT_free(x, u) mm_heap_free(x)
#define STBTT_memset(p, v, n) memset((p), (char)(v), (n))
#define STBTT_memcpy(d, s, n) memcpy((d), (s), (n))
static size_t stb_strlen_k(const char *s) {
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}
#define STBTT_strlen(x) stb_strlen_k(x)
#define STB_TRUETYPE_IMPLEMENTATION
#include "user/lib/stb_truetype.h"

#define FONT_SIZE_PX 17
#define ATLAS_W 512
#define ATLAS_H 512
#define FIRST_CP 0x20
#define NUM_CP 96

void *font_atlas_load_from_ttf(uint32_t *out_size) {
    uint32_t ttf_size = 0;
    uint8_t *ttf_data = fat_read_file_path("/SYS/DAT/FONT.TTF", &ttf_size);
    if (!ttf_data)
        return 0;

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf_data, 0)) {
        mm_heap_free(ttf_data);
        return 0;
    }

    float scale = stbtt_ScaleForPixelHeight(&info, (float)FONT_SIZE_PX);
    int ascent_raw, descent_raw, line_gap_raw;
    stbtt_GetFontVMetrics(&info, &ascent_raw, &descent_raw, &line_gap_raw);
    int ascent_px = (int)(ascent_raw * scale + 0.5f);
    int line_height = (int)((ascent_raw - descent_raw + line_gap_raw) * scale + 0.5f);

    uint8_t *gray = (uint8_t *)mm_heap_alloc(ATLAS_W * ATLAS_H);
    if (!gray) {
        mm_heap_free(ttf_data);
        return 0;
    }
    memset(gray, 0, ATLAS_W * ATLAS_H);

    stbtt_bakedchar baked[NUM_CP];
    stbtt_BakeFontBitmap(ttf_data, 0, (float)FONT_SIZE_PX, gray, ATLAS_W, ATLAS_H, FIRST_CP, NUM_CP,
                         baked);
    mm_heap_free(ttf_data);

    uint32_t glyph_block = (uint32_t)(NUM_CP * (int)sizeof(GlyphInfo));
    uint32_t bitmap_block = ATLAS_W * ATLAS_H * 4;
    uint32_t atlas_size = 24 + glyph_block + bitmap_block;
    uint8_t *atlas = (uint8_t *)mm_heap_alloc(atlas_size);
    if (!atlas) {
        mm_heap_free(gray);
        return 0;
    }
    memset(atlas, 0, atlas_size);

    uint32_t *hdr = (uint32_t *)atlas;
    hdr[0] = 0x464F4E54;
    hdr[1] = ATLAS_W;
    hdr[2] = ATLAS_H;
    hdr[3] = (uint32_t)line_height;
    hdr[4] = (uint32_t)ascent_px;
    hdr[5] = NUM_CP;

    GlyphInfo *glyphs = (GlyphInfo *)(atlas + 24);
    for (int i = 0; i < NUM_CP; i++) {
        glyphs[i].codepoint = (uint32_t)(FIRST_CP + i);
        glyphs[i].x = baked[i].x0;
        glyphs[i].y = baked[i].y0;
        glyphs[i].w = (uint16_t)(baked[i].x1 - baked[i].x0);
        glyphs[i].h = (uint16_t)(baked[i].y1 - baked[i].y0);
        glyphs[i].advance = (int16_t)(baked[i].xadvance + 0.5f);
        glyphs[i].off_x = (int16_t)(baked[i].xoff);
        glyphs[i].off_y = (int16_t)(ascent_px + (int)(baked[i].yoff));
    }

    uint32_t *bitmap = (uint32_t *)(atlas + 24 + glyph_block);
    for (int i = 0; i < ATLAS_W * ATLAS_H; i++)
        bitmap[i] = (uint32_t)gray[i] << 24;

    mm_heap_free(gray);
    if (out_size)
        *out_size = atlas_size;
    return atlas;
}
