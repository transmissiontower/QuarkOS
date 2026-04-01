#include "video/texture.h"
#include "drivers/GUI/framebuffer.h"
#include "lib/math.h"
#include "mm/kmalloc.h"
#include "util/util.h"

uint8_t *shadow_corner = 0;
uint8_t *shadow_edge = 0;
int shadow_size = 0;
int shadow_radius = 0;

Texture *create_texture(int w, int h) {
    Texture *tex = (Texture *)kmalloc(sizeof(Texture));
    tex->width = w;
    tex->height = h;
    tex->data = (uint32_t *)kmalloc(w * h * 4);
    memset(tex->data, 0, w * h * 4);
    return tex;
}

void draw_texture(Texture *tex, int x, int y) {
    if (!tex)
        return;

    int dest_x = x;
    int dest_y = y;
    int src_x = 0;
    int src_y = 0;
    int draw_w = tex->width;
    int draw_h = tex->height;

    if (dest_x < 0) {
        src_x = -dest_x;
        draw_w += dest_x;
        dest_x = 0;
    }
    if (dest_y < 0) {
        src_y = -dest_y;
        draw_h += dest_y;
        dest_y = 0;
    }

    if (dest_x + draw_w > (int)fb_width) {
        draw_w = (int)fb_width - dest_x;
    }
    if (dest_y + draw_h > (int)fb_height) {
        draw_h = (int)fb_height - dest_y;
    }

    if (draw_w <= 0 || draw_h <= 0)
        return;

    for (int ly = 0; ly < draw_h; ly++) {
        int screen_y = dest_y + ly;
        int tex_row = src_y + ly;

        for (int lx = 0; lx < draw_w; lx++) {
            int screen_x = dest_x + lx;
            int tex_col = src_x + lx;

            uint32_t src = tex->data[tex_row * tex->width + tex_col];
            uint8_t alpha = (src >> 24) & 0xFF;

            if (alpha == 0)
                continue;

            if (alpha == 255) {
                put_pixel(screen_x, screen_y, src);
            } else {
                put_pixel_AA(screen_x, screen_y, src, alpha);
            }
        }
    }
}

void shadows_init(int size, int r, int intensity) {
    shadow_size = size;
    shadow_radius = r;

    shadow_edge = (uint8_t *)kmalloc(size);
    for (int i = 0; i < size; i++) {
        double t = (double)i / size;
        double alpha_f = (1.0 - t) * (1.0 - t);
        shadow_edge[i] = (uint8_t)(alpha_f * intensity);
    }

    int dim = r + size;
    shadow_corner = (uint8_t *)kmalloc(dim * dim);

    for (int y = 0; y < dim; y++) {
        for (int x = 0; x < dim; x++) {
            double dist_from_center = sqrt((double)(x + 1) * (x + 1) + (double)(y + 1) * (y + 1));

            int dist = (int)(dist_from_center - r);

            if (dist < 0) {
                shadow_corner[y * dim + x] = 0;
            } else if (dist >= size) {
                shadow_corner[y * dim + x] = 0;
            } else {
                shadow_corner[y * dim + x] = shadow_edge[dist];
            }
        }
    }
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

uint8_t *corner_mask_texture(int r) {
    uint8_t *mask = (uint8_t *)kmalloc(r * r);

    int r_fp = r << 8;

    for (int y = 0; y < r; y++) {
        for (int x = 0; x < r; x++) {
            int dist_x = (r - 1) - x;
            int dist_y = (r - 1) - y;

            uint32_t dist = isqrt64(((uint64_t)dist_x * dist_x + (uint64_t)dist_y * dist_y) << 16);

            int delta = r_fp - (int)dist;
            int alpha = 127 + delta;

            if (alpha < 0)
                alpha = 0;
            if (alpha > 255)
                alpha = 255;

            mask[y * r + x] = (uint8_t)alpha;
        }
    }
    return mask;
}