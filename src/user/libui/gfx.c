#include "user/libui/gfx.h"
#include "kernel/lib/math.h"
#include "stdlib.h"
#include "user/libc/libapi.h"
static uint32_t local_isqrt64(uint64_t n) {
    uint64_t res = 0;
    uint64_t bit = 1ULL << 62;
    while (bit > n)
        bit >>= 2;
    while (bit != 0) {
        if (n >= res + bit) {
            n -= res + bit;
            res = (res >> 1) + bit;
        } else
            res >>= 1;
        bit >>= 2;
    }
    return (uint32_t)res;
}
static inline int local_abs(int n) {
    return (n < 0) ? -n : n;
}
Texture *create_texture(int w, int h) {
    Texture *t = (Texture *)malloc(sizeof(Texture));
    if (!t)
        return 0;
    t->w = w;
    t->h = h;
    t->data = (uint32_t *)malloc(w * h * sizeof(uint32_t));
    if (t->data) {
        memset32(t->data, 0, w * h);
    }
    return t;
}
uint32_t gfx_blend_colors(uint32_t fg, uint32_t bg, uint8_t alpha) {
    if (alpha == 255)
        return fg;
    if (alpha == 0)
        return bg;
    uint32_t r1 = (fg >> 16) & 0xFF, g1 = (fg >> 8) & 0xFF, b1 = fg & 0xFF;
    uint32_t r2 = (bg >> 16) & 0xFF, g2 = (bg >> 8) & 0xFF, b2 = bg & 0xFF;
    uint32_t inv = 255 - alpha;
    uint32_t r = (r1 * alpha + r2 * inv) >> 8;
    uint32_t g = (g1 * alpha + g2 * inv) >> 8;
    uint32_t b = (b1 * alpha + b2 * inv) >> 8;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}
void gfx_put_pixel_aa(gfx_context_t *ctx, int x, int y, uint32_t color, uint8_t alpha) {
    if (x < 0 || x >= (int)ctx->width || y < 0 || y >= (int)ctx->height)
        return;
    uint32_t bg = ctx->buffer[y * ctx->width + x];
    ctx->buffer[y * ctx->width + x] = gfx_blend_colors(color, bg, alpha);
}
void gfx_put_pixel(gfx_context_t *ctx, int x, int y, uint32_t color) {
    if (x < 0 || x >= (int)ctx->width || y < 0 || y >= (int)ctx->height)
        return;
    ctx->buffer[y * ctx->width + x] = color;
}
void gfx_rect_fill(gfx_context_t *ctx, int x, int y, int w, int h, uint32_t color) {
    for (int i = 0; i < h; i++) {
        int py = y + i;
        if (py < 0 || py >= (int)ctx->height)
            continue;
        for (int j = 0; j < w; j++) {
            int px = x + j;
            if (px < 0 || px >= (int)ctx->width)
                continue;
            ctx->buffer[py * ctx->width + px] = color;
        }
    }
}
void gfx_rect_rounded_fill(gfx_context_t *ctx, int x, int y, int w, int h, int r, uint32_t color) {
    if (r <= 0) {
        gfx_rect_fill(ctx, x, y, w, h, color);
        return;
    }
    gfx_rect_fill(ctx, x + r, y, w - 2 * r, h, color);
    gfx_rect_fill(ctx, x, y + r, r, h - 2 * r, color);
    gfx_rect_fill(ctx, x + w - r, y + r, r, h - 2 * r, color);
    int r_fp = r << 8;
    int centers[4][2] = {{x + r, y + r},
                         {x + w - r - 1, y + r},
                         {x + r, y + h - r - 1},
                         {x + w - r - 1, y + h - r - 1}};
    for (int j = 0; j <= r; j++) {
        for (int i = 0; i <= r; i++) {
            int di = 2 * i - 1;
            int dj = 2 * j - 1;
            uint32_t dist = local_isqrt64(((uint64_t)di * di + (uint64_t)dj * dj) << 14);
            int alpha = r_fp - (int)dist + 128;
            if (alpha <= 0)
                continue;
            if (alpha > 255)
                alpha = 255;
            gfx_put_pixel_aa(ctx, centers[0][0] - i, centers[0][1] - j, color, (uint8_t)alpha);
            gfx_put_pixel_aa(ctx, centers[1][0] + i, centers[1][1] - j, color, (uint8_t)alpha);
            gfx_put_pixel_aa(ctx, centers[2][0] - i, centers[2][1] + j, color, (uint8_t)alpha);
            gfx_put_pixel_aa(ctx, centers[3][0] + i, centers[3][1] + j, color, (uint8_t)alpha);
        }
    }
}
void draw_rounded_rect(gfx_context_t *ctx, int x, int y, int w, int h, int r, uint32_t color,
                       uint8_t *mask) {
    if (!mask) {
        gfx_rect_rounded_fill(ctx, x, y, w, h, r, color);
        return;
    }
    gfx_rect_fill(ctx, x + r, y, w - 2 * r, h, color);
    gfx_rect_fill(ctx, x, y + r, r, h - 2 * r, color);
    gfx_rect_fill(ctx, x + w - r, y + r, r, h - 2 * r, color);
    for (int i = 0; i < r; i++) {
        for (int j = 0; j < r; j++) {
            uint8_t a = mask[j * r + i];
            if (a)
                gfx_put_pixel_aa(ctx, x + i, y + j, color, a);
        }
    }
    for (int i = 0; i < r; i++) {
        for (int j = 0; j < r; j++) {
            uint8_t a = mask[j * r + (r - 1 - i)];
            if (a)
                gfx_put_pixel_aa(ctx, x + w - r + i, y + j, color, a);
        }
    }
    for (int i = 0; i < r; i++) {
        for (int j = 0; j < r; j++) {
            uint8_t a = mask[(r - 1 - j) * r + i];
            if (a)
                gfx_put_pixel_aa(ctx, x + i, y + h - r + j, color, a);
        }
    }
    for (int i = 0; i < r; i++) {
        for (int j = 0; j < r; j++) {
            uint8_t a = mask[(r - 1 - j) * r + (r - 1 - i)];
            if (a)
                gfx_put_pixel_aa(ctx, x + w - r + i, y + h - r + j, color, a);
        }
    }
}
void gfx_draw_shadow(gfx_context_t *ctx, int x, int y, int w, int h, int r, uint8_t opacity) {
    int s_size = 6;
    for (int j = -s_size; j < h + s_size; j++) {
        for (int i = -s_size; i < w + s_size; i++) {
            int dx = (i < r) ? r - i : (i >= w - r ? i - (w - r - 1) : 0);
            int dy = (j < r) ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
            uint32_t dist;
            if (dx > 0 && dy > 0)
                dist = local_isqrt64(((uint64_t)dx * dx + (uint64_t)dy * dy) << 16) >> 8;
            else
                dist = (uint32_t)((dx > dy) ? dx : dy);
            if (i >= 0 && i < w && j >= 0 && j < h && dist < (uint32_t)r)
                continue;
            if (dist < (uint32_t)(r + s_size)) {
                int factor = (dist < (uint32_t)r) ? 0 : (int)(dist - (uint32_t)r);
                uint8_t a = (uint8_t)((s_size - factor) * (int)opacity / s_size);
                if (a > 2)
                    gfx_put_pixel_aa(ctx, x + i + 2, y + j + 2, 0, a);
            }
        }
    }
}
void gfx_draw_sprite(gfx_context_t *ctx, int x, int y, int w, int h, const uint8_t *data,
                     const uint32_t *palette) {
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            int px = x + j, py = y + i;
            if (px < 0 || px >= (int)ctx->width || py < 0 || py >= (int)ctx->height)
                continue;
            uint8_t idx = data[i * w + j];
            if (idx != 0)
                ctx->buffer[py * ctx->width + px] = palette[idx];
        }
    }
}
void draw_texture(gfx_context_t *ctx, Texture *t, int x, int y) {
    if (!t || !t->data)
        return;
    for (int i = 0; i < t->h; i++) {
        for (int j = 0; j < t->w; j++) {
            uint32_t c = t->data[i * t->w + j];
            if ((c >> 24) != 0)
                gfx_put_pixel_aa(ctx, x + j, y + i, c, (c >> 24));
        }
    }
}
uint8_t *corner_mask_texture(int r) {
    uint8_t *mask = malloc(r * r);
    int r_fp = r << 8;
    for (int j = 0; j < r; j++) {
        for (int i = 0; i < r; i++) {
            int di = 2 * (r - i) - 1;
            int dj = 2 * (r - j) - 1;
            uint32_t dist = local_isqrt64(((uint64_t)di * di + (uint64_t)dj * dj) << 14);
            int alpha = r_fp - (int)dist + 128;
            if (alpha < 0)
                alpha = 0;
            if (alpha > 255)
                alpha = 255;
            mask[j * r + i] = (uint8_t)alpha;
        }
    }
    return mask;
}
Texture *rhombus_rounded(int w, int h, int r, uint32_t color) {
    int size = (int)sqrt(w * w + h * h) + 4;
    Texture *tex = create_texture(size, size);
    double cx = size / 2.0;
    double cy = size / 2.0;
    double box_w = w / 2.0 - r;
    double box_h = h / 2.0 - r;
    double angle = 45.0 * PI / 180.0;
    double c = cos(-angle);
    double s = sin(-angle);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            double dx = x - cx;
            double dy = y - cy;
            double rx = dx * c - dy * s;
            double ry = dx * s + dy * c;
            double px = fabs(rx);
            double py = fabs(ry);
            double qx = px - box_w;
            double qy = py - box_h;
            double out_x = (qx > 0) ? qx : 0;
            double out_y = (qy > 0) ? qy : 0;
            double dist_out = sqrt(out_x * out_x + out_y * out_y);
            double inside_val = (qx > qy) ? qx : qy;
            double dist_in = (inside_val > 0) ? 0 : inside_val;
            double dist = dist_out + dist_in - r;
            double alpha_f = 0.5 - dist;
            if (alpha_f < 0)
                alpha_f = 0;
            if (alpha_f > 1)
                alpha_f = 1;
            if (alpha_f > 0) {
                uint8_t alpha = (uint8_t)(alpha_f * 255.0);
                tex->data[y * size + x] = (color & 0x00FFFFFF) | ((uint32_t)alpha << 24);
            }
        }
    }
    return tex;
}
