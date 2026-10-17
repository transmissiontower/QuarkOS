// SPDX-License-Identifier: MIT
#include "user/lib/font_atlas.h"
#include "user/lib/libapi.h"
#include "lib/stdlib.h"
#include "lib/math.h"
static double stb_ceil(double x) {
    int i = (int)x;
    return (double)i + (x > (double)i ? 1.0 : 0.0);
}
static double stb_acos(double x) {
    return 1.5707963 - (x + x * x * x / 6.0 + x * x * x * x * x * 3.0 / 40.0);
}
#define STBTT_ifloor(x) ((int)floor((double)(x)))
#define STBTT_iceil(x) ((int)stb_ceil((double)(x)))
#define STBTT_sqrt(x) sqrt((double)(x))
#define STBTT_pow(x, y) sqrt((double)(x))
#define STBTT_fmod(x, y) fmod((double)(x), (double)(y))
#define STBTT_cos(x) cos((double)(x))
#define STBTT_acos(x) stb_acos((double)(x))
#define STBTT_fabs(x) fabs((double)(x))
#define STBTT_assert(x) ((void)0)
#define STBTT_malloc(x, u) malloc(x)
#define STBTT_free(x, u) free(x)
#define STB_TRUETYPE_IMPLEMENTATION
#include "user/lib/stb_truetype.h"

AtlasFont *user_font = 0;
AtlasFont *gui_icons = 0;

#define FONT_MAGIC_ATLAS 0x464F4E54
#define DEFAULT_SIZE 17

static stbtt_fontinfo g_ttf;
static int g_ttf_ready = 0;
static float g_scale = 0.0f;
static int g_ascent_px = 0;
static int g_line_h = 0;
static uint8_t *g_ttf_data = 0;

#define CACHE_SZ 128
typedef struct {
    uint8_t *bm;
    int w, h, xoff, yoff, adv;
} CGlyph;
static CGlyph g_cache[CACHE_SZ];
static int g_cache_sz_used = DEFAULT_SIZE;

static CGlyph *glyph_get(uint32_t cp, int size) {
    float sc = stbtt_ScaleForPixelHeight(&g_ttf, (float)size);
    if (cp < CACHE_SZ) {
        CGlyph *cg = &g_cache[cp];
        if (!cg->bm || g_cache_sz_used != size) {
            if (cg->bm)
                free(cg->bm);
            cg->bm = stbtt_GetCodepointBitmap(&g_ttf, 0, sc, (int)cp, &cg->w, &cg->h, &cg->xoff,
                                              &cg->yoff);
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&g_ttf, (int)cp, &adv, &lsb);
            cg->adv = (int)(adv * sc + 0.5f);
        }
        return cg;
    }

    static CGlyph tmp;
    float sc2 = stbtt_ScaleForPixelHeight(&g_ttf, (float)size);
    tmp.bm =
        stbtt_GetCodepointBitmap(&g_ttf, 0, sc2, (int)cp, &tmp.w, &tmp.h, &tmp.xoff, &tmp.yoff);
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&g_ttf, (int)cp, &adv, &lsb);
    tmp.adv = (int)(adv * sc2 + 0.5f);
    return &tmp;
}

static void ttf_init(uint8_t *data) {
    if (!stbtt_InitFont(&g_ttf, data, 0))
        return;
    g_ttf_data = data;
    g_ttf_ready = 1;
    g_scale = stbtt_ScaleForPixelHeight(&g_ttf, (float)DEFAULT_SIZE);
    int asc, desc, lg;
    stbtt_GetFontVMetrics(&g_ttf, &asc, &desc, &lg);
    g_ascent_px = (int)(asc * g_scale + 0.5f);
    g_line_h = (int)((asc - desc + lg) * g_scale + 0.5f);
    if (g_line_h < DEFAULT_SIZE)
        g_line_h = DEFAULT_SIZE + 2;

    user_font = (AtlasFont *)malloc(sizeof(AtlasFont));
    if (!user_font)
        return;
    user_font->tex_width = 0;
    user_font->tex_height = 0;
    user_font->line_height = (uint32_t)g_line_h;
    user_font->ascent = (uint32_t)g_ascent_px;
    user_font->count = 0;
    user_font->glyphs = 0;
    user_font->bitmap = 0;
    g_cache_sz_used = DEFAULT_SIZE;
}

void font_atlas_init(uint8_t *raw_data) {
    if (!raw_data)
        return;
    uint32_t magic = *(uint32_t *)raw_data;
    if (magic == FONT_MAGIC_ATLAS) {
        user_font = (AtlasFont *)malloc(sizeof(AtlasFont));
        if (!user_font)
            return;
        user_font->tex_width = *(uint32_t *)(raw_data + 4);
        user_font->tex_height = *(uint32_t *)(raw_data + 8);
        user_font->line_height = *(uint32_t *)(raw_data + 12);
        user_font->ascent = *(uint32_t *)(raw_data + 16);
        user_font->count = *(uint32_t *)(raw_data + 20);
        user_font->glyphs = (GlyphInfo *)(raw_data + 24);
        user_font->bitmap = (uint32_t *)(raw_data + 24 + user_font->count * sizeof(GlyphInfo));
    } else {
        ttf_init(raw_data);
    }
}

void font_atlas_init_for_ptr(AtlasFont *atlas, uint8_t *raw_data) {
    if (!raw_data || !atlas || *(uint32_t *)raw_data != FONT_MAGIC_ATLAS)
        return;
    atlas->tex_width = *(uint32_t *)(raw_data + 4);
    atlas->tex_height = *(uint32_t *)(raw_data + 8);
    atlas->line_height = *(uint32_t *)(raw_data + 12);
    atlas->ascent = *(uint32_t *)(raw_data + 16);
    atlas->count = *(uint32_t *)(raw_data + 20);
    atlas->glyphs = (GlyphInfo *)(raw_data + 24);
    atlas->bitmap = (uint32_t *)(raw_data + 24 + atlas->count * sizeof(GlyphInfo));
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

static GlyphInfo *glyph_find_atlas(AtlasFont *font, uint32_t cp) {
    if (!font || !font->glyphs)
        return 0;
    int lo = 0, hi = (int)font->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;

        const uint8_t *_b = (const uint8_t *)&font->glyphs[mid].codepoint;
        uint32_t mcp = (uint32_t)_b[0] | ((uint32_t)_b[1] << 8) | ((uint32_t)_b[2] << 16) |
                       ((uint32_t)_b[3] << 24);
        if (mcp == cp)
            return &font->glyphs[mid];
        if (mcp < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

int get_codepoint_width(uint32_t cp) {
    if (g_ttf_ready) {
        if (cp < CACHE_SZ)
            return glyph_get(cp, DEFAULT_SIZE)->adv;
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&g_ttf, (int)cp, &adv, &lsb);
        return (int)(adv * g_scale + 0.5f);
    }
    if (!user_font)
        return 0;
    GlyphInfo *g = glyph_find_atlas(user_font, cp);
    return g ? g->advance : (int)user_font->line_height / 2;
}

int get_char_width(char c) {
    return get_codepoint_width((uint32_t)(uint8_t)c);
}

int get_string_width(const char *str) {
    if (!str)
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

int get_font_height(void) {
    if (g_ttf_ready)
        return g_line_h;
    return user_font ? (int)user_font->line_height : 0;
}

int draw_codepoint_vector(gfx_context_t *ctx, int x, int y, uint32_t cp, uint32_t color) {
    if (g_ttf_ready) {
        CGlyph *cg = glyph_get(cp, DEFAULT_SIZE);
        if (!cg || !cg->bm)
            return x + cg->adv;
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g2 = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;
        for (int iy = 0; iy < cg->h; iy++) {
            for (int ix = 0; ix < cg->w; ix++) {
                uint8_t a = cg->bm[iy * cg->w + ix];
                if (a > 0) {
                    uint32_t c2 =
                        ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g2 << 8) | b;
                    gfx_put_pixel_aa(ctx, x + cg->xoff + ix, y + g_ascent_px + cg->yoff + iy, c2,
                                     a);
                }
            }
        }
        return x + cg->adv;
    }
    if (!user_font)
        return x;
    GlyphInfo *g = glyph_find_atlas(user_font, cp);
    if (!g)
        return x + (int)user_font->line_height / 2;
    if (g->w == 0)
        return x + g->advance;
    int top_y = y + g->off_y;
    for (int iy = 0; iy < g->h; iy++) {
        for (int ix = 0; ix < g->w; ix++) {
            uint32_t px = user_font->bitmap[(g->y + iy) * user_font->tex_width + (g->x + ix)];
            uint8_t a = (px >> 24) & 0xFF;
            if (a > 0)
                gfx_put_pixel_aa(ctx, x + g->off_x + ix, top_y + iy, color, a);
        }
    }
    return x + g->advance;
}

int draw_char_vector(gfx_context_t *ctx, int x, int y, char c, uint32_t color) {
    return draw_codepoint_vector(ctx, x, y, (uint32_t)(uint8_t)c, color);
}

void draw_string_vector(gfx_context_t *ctx, int x, int y, const char *str, uint32_t color) {
    if (!str)
        return;
    int lh = get_font_height();
    int start_x = x;
    const uint8_t *p = (const uint8_t *)str;
    while (*p) {
        if (*p == '\n') {
            y += lh;
            x = start_x;
            p++;
            continue;
        }
        uint32_t cp;
        int n = utf8_decode(p, &cp);
        x = draw_codepoint_vector(ctx, x, y, cp, color);
        p += n;
    }
}

int draw_codepoint_ex(gfx_context_t *ctx, int x, int y, uint32_t cp, uint32_t color, int size,
                      int flags) {
    (void)flags;
    if (!g_ttf_ready)
        return draw_codepoint_vector(ctx, x, y, cp, color);
    float sc = stbtt_ScaleForPixelHeight(&g_ttf, (float)size);
    int asc, desc, lg;
    stbtt_GetFontVMetrics(&g_ttf, &asc, &desc, &lg);
    int asc_px = (int)(asc * sc + 0.5f);
    int w, h, xoff, yoff;
    uint8_t *bm = stbtt_GetCodepointBitmap(&g_ttf, 0, sc, (int)cp, &w, &h, &xoff, &yoff);
    if (!bm)
        return x;
    uint8_t r = (color >> 16) & 0xFF, g2 = (color >> 8) & 0xFF, b = color & 0xFF;
    for (int iy = 0; iy < h; iy++)
        for (int ix = 0; ix < w; ix++) {
            uint8_t a = bm[iy * w + ix];
            if (a > 0) {
                uint32_t c2 = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g2 << 8) | b;
                gfx_put_pixel_aa(ctx, x + xoff + ix, y + asc_px + yoff + iy, c2, a);
            }
        }
    free(bm);
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&g_ttf, (int)cp, &adv, &lsb);
    return x + (int)(adv * sc + 0.5f);
}

int draw_char_ex(gfx_context_t *ctx, int x, int y, char c, uint32_t color, int size, int flags) {
    return draw_codepoint_ex(ctx, x, y, (uint32_t)(uint8_t)c, color, size, flags);
}

void draw_string_ex(gfx_context_t *ctx, int x, int y, const char *str, uint32_t color, int size,
                    int flags) {
    if (!str)
        return;
    float sc = g_ttf_ready ? stbtt_ScaleForPixelHeight(&g_ttf, (float)size) : 0;
    int lh = size + 2;
    if (g_ttf_ready) {
        int asc, desc, lg;
        stbtt_GetFontVMetrics(&g_ttf, &asc, &desc, &lg);
        lh = (int)((asc - desc + lg) * sc + 0.5f);
    }
    int start_x = x;
    const uint8_t *p = (const uint8_t *)str;
    while (*p) {
        if (*p == '\n') {
            y += lh;
            x = start_x;
            p++;
            continue;
        }
        uint32_t cp;
        int n = utf8_decode(p, &cp);
        x = draw_codepoint_ex(ctx, x, y, cp, color, size, flags);
        p += n;
    }
}

int measure_codepoint_ex(uint32_t cp, int size, int flags) {
    (void)flags;
    if (!g_ttf_ready)
        return get_codepoint_width(cp);
    float sc = stbtt_ScaleForPixelHeight(&g_ttf, (float)size);
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&g_ttf, (int)cp, &adv, &lsb);
    return (int)(adv * sc + 0.5f);
}

int measure_string_ex(const char *str, int size, int flags) {
    if (!str)
        return 0;
    int w = 0;
    const uint8_t *p = (const uint8_t *)str;
    while (*p) {
        uint32_t cp;
        int n = utf8_decode(p, &cp);
        w += measure_codepoint_ex(cp, size, flags);
        p += n;
    }
    return w;
}

int get_line_height_ex(int size) {
    if (!g_ttf_ready)
        return size + 2;
    float sc = stbtt_ScaleForPixelHeight(&g_ttf, (float)size);
    int asc, desc, lg;
    stbtt_GetFontVMetrics(&g_ttf, &asc, &desc, &lg);
    return (int)((asc - desc + lg) * sc + 0.5f);
}

void draw_icon(gfx_context_t *ctx, AtlasFont *atlas, int x, int y, uint32_t icon_id,
               uint32_t color) {
    if (!atlas || icon_id < 32)
        return;
    GlyphInfo *g = glyph_find_atlas(atlas, icon_id);
    if (!g)
        return;
    for (int iy = 0; iy < g->h; iy++)
        for (int ix = 0; ix < g->w; ix++) {
            uint32_t px = atlas->bitmap[(g->y + iy) * atlas->tex_width + (g->x + ix)];
            uint8_t a = (px >> 24) & 0xFF;
            if (a > 0)
                gfx_put_pixel_aa(ctx, x + g->off_x + ix, y + g->off_y + iy, color, a);
        }
}

void draw_icon_sized(gfx_context_t *ctx, AtlasFont *atlas, int x, int y, uint32_t icon_id,
                     uint32_t color, int size) {
    if (!atlas || icon_id < 32 || size <= 0)
        return;
    GlyphInfo *g = glyph_find_atlas(atlas, icon_id);
    if (!g || g->w == 0 || g->h == 0)
        return;
    for (int oy = 0; oy < size; oy++) {
        int sy0 = oy * g->h / size, sy1 = (oy + 1) * g->h / size;
        if (sy1 <= sy0)
            sy1 = sy0 + 1;
        for (int ox = 0; ox < size; ox++) {
            int sx0 = ox * g->w / size, sx1 = (ox + 1) * g->w / size;
            if (sx1 <= sx0)
                sx1 = sx0 + 1;
            uint32_t sum = 0, count = 0;
            for (int sy = sy0; sy < sy1; sy++)
                for (int sx = sx0; sx < sx1; sx++) {
                    uint32_t px = atlas->bitmap[(g->y + sy) * atlas->tex_width + (g->x + sx)];
                    sum += (px >> 24) & 0xFF;
                    count++;
                }
            uint8_t a = (uint8_t)(sum / count);
            if (a > 0)
                gfx_put_pixel_aa(ctx, x + ox, y + oy, color, a);
        }
    }
}
