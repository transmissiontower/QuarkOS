#include "drivers/GUI/framebuffer.h"
#include "arch/x86/multiboot.h"
#include "lib/math.h"
#include "lib/stdint.h"
#include "lib/stdlib.h"
#include "mm/kmalloc.h"
#include "mm/memory.h"
#include "util/util.h"
#include "video/font.h"
#include "video/texture.h"

uint32_t *fb_ptr = 0;
uint8_t *back_buffer = 0;

uint32_t fb_width = 0;
uint32_t fb_height = 0;
uint32_t fb_pitch = 0;
uint32_t fb_bpp = 0;
uint32_t fb_size_bytes = 0;

void framebuffer_init(struct multiboot_info *mb_info) {
    fb_width = mb_info->framebuffer_width;
    fb_height = mb_info->framebuffer_height;
    fb_pitch = mb_info->framebuffer_pitch;
    fb_bpp = mb_info->framebuffer_bpp;

    if (fb_pitch == 0) {
        fb_pitch = fb_width * (fb_bpp / 8);
    }

    uint32_t phys_addr = (uint32_t)mb_info->framebuffer_addr;
    fb_size_bytes = fb_height * fb_pitch;

    uint32_t virt_addr_start = 0xF0000000;
    for (uint32_t offset = 0; offset < fb_size_bytes + 4096; offset += 4096) {
        mem_map_page(virt_addr_start + offset, phys_addr + offset, PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE);
    }
    fb_ptr = (uint32_t *)virt_addr_start;

    if (back_buffer == 0) {
        back_buffer = (uint8_t *)kmalloc(fb_size_bytes);
    }

    if (back_buffer != 0) {
        memset(back_buffer, 0, fb_size_bytes);
    }
}

void wait_vblank() { }

uint32_t fast_dist_fp(int x, int y) {
    uint64_t dist_sq = (uint64_t)x * x + (uint64_t)y * y;
    return isqrt64(dist_sq << 16);
}

void put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= (int)fb_width || y < 0 || y >= (int)fb_height)
        return;
    uint32_t offset = (y * fb_pitch) + (x * 4);
    *(uint32_t *)(back_buffer + offset) = color;
}

void put_pixel_AA(int x, int y, uint32_t color, uint8_t alpha) {
    if (x < 0 || x >= (int)fb_width || y < 0 || y >= (int)fb_height)
        return;
    uint32_t bg = get_pixel(x, y);
    uint32_t final_color = blend_colors(color, bg, alpha);
    put_pixel(x, y, final_color);
}

uint32_t get_pixel(int x, int y) {
    if (x < 0 || x >= (int)fb_width || y < 0 || y >= (int)fb_height)
        return 0;
    uint32_t offset = (y * fb_pitch) + (x * 4);
    return *(uint32_t *)(back_buffer + offset);
}

void plot_pixel(int x, int y, uint8_t alpha) {
    if (x < 0 || x >= (int)fb_width || y < 0 || y >= (int)fb_height)
        return;

    uint32_t offset = (y * fb_pitch) + (x * 4);
    uint32_t bg = *(uint32_t *)(back_buffer + offset);

    uint32_t inv_a = 255 - alpha;
    uint32_t r = ((bg >> 16) & 0xFF) * inv_a >> 8;
    uint32_t g = ((bg >> 8) & 0xFF) * inv_a >> 8;
    uint32_t b = (bg & 0xFF) * inv_a >> 8;

    *(uint32_t *)(back_buffer + offset) = 0xFF000000 | (r << 16) | (g << 8) | b;
}

void rect_fill(int x, int y, int w, int h, uint32_t color) {
    int x1 = x;
    int y1 = y;
    int x2 = x + w;
    int y2 = y + h;

    if (x1 < 0)
        x1 = 0;
    if (y1 < 0)
        y1 = 0;
    if (x2 > (int)fb_width)
        x2 = (int)fb_width;
    if (y2 > (int)fb_height)
        y2 = (int)fb_height;

    if (x1 >= x2 || y1 >= y2)
        return;

    int new_w = x2 - x1;
    int new_h = y2 - y1;

    for (int i = 0; i < new_h; i++) {
        uint32_t offset = ((y1 + i) * fb_pitch) + (x1 * 4);
        memset32(back_buffer + offset, color, new_w);
    }
}

void rect_draw(int cx, int cy, int w, int h, int32_t d, uint32_t color) {
    if (d == 0) {
        rect_fill(cx - w / 2, cy - h / 2, w, 1, color);
        rect_fill(cx - w / 2, cy + h / 2, w, 1, color);
        rect_fill(cx - w / 2, cy - h / 2, 1, h, color);
        rect_fill(cx + w / 2, cy - h / 2, 1, h, color);
        return;
    }
    int32_t c = i_cos(d);
    int32_t s = i_sin(d);
    int limit = (w > h ? w : h);
    int hw = w / 2;
    int hh = h / 2;
    for (int y = -limit; y <= limit; y++) {
        for (int x = -limit; x <= limit; x++) {
            int32_t lx = ((int64_t)x * c + (int64_t)y * s) >> FP_SHIFT;
            int32_t ly = ((int64_t)-x * s + (int64_t)y * c) >> FP_SHIFT;
            if (lx >= -hw && lx <= hw && ly >= -hh && ly <= hh) {
                if (lx == -hw || lx == hw || ly == -hh || ly == hh)
                    put_pixel(cx + x, cy + y, color);
            }
        }
    }
}

void circle_draw_AA(int xc, int yc, int r, uint32_t color) {
    int r_fp = r << 8;
    int r_sq = r * r;
    for (int y = -r - 1; y <= r + 1; y++) {
        int width = 0;
        if (y * y < r_sq) {
            width = isqrt64((uint64_t)(r_sq - y * y) << 16) >> 8;
        }
        int start_x = (width > 2) ? width - 2 : 0;
        int end_x = width + 2;
        for (int x = start_x; x <= end_x; x++) {
            uint32_t dist = fast_dist_fp(x, y);
            int delta = r_fp - (int)dist;
            if (delta < 0)
                delta = -delta;
            if (delta < 256) {
                int alpha = 255 - delta;
                if (alpha > 0) {
                    put_pixel_AA(xc + x, yc + y, color, (uint8_t)alpha);
                    if (x != 0)
                        put_pixel_AA(xc - x, yc + y, color, (uint8_t)alpha);
                }
            }
        }
    }
}

void circle_fill(int xc, int yc, int r, uint32_t color) {
    int x = 0;
    int y = r;
    int d = 3 - 2 * r;
    while (y >= x) {
        rect_fill(xc - y, yc - x, 2 * y + 1, 1, color);
        rect_fill(xc - y, yc + x, 2 * y + 1, 1, color);
        rect_fill(xc - x, yc - y, 2 * x + 1, 1, color);
        rect_fill(xc - x, yc + y, 2 * x + 1, 1, color);
        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

void circle_fill_AA(int xc, int yc, int r, uint32_t color) {
    int r_fp = r << 8;
    int r_sq = r * r;
    for (int y = -r - 1; y <= r + 1; y++) {
        int width = 0;
        if (y * y < r_sq)
            width = isqrt64((uint64_t)(r_sq - y * y) << 16) >> 8;
        if (width > 2)
            rect_fill(xc - width + 2, yc + y, (width - 2) * 2 + 1, 1, color);
        int start_x = (width > 1) ? width - 1 : 0;
        int end_x = width + 2;
        for (int x = start_x; x <= end_x; x++) {
            uint32_t dist = fast_dist_fp(x, y);
            int delta = r_fp - dist;
            int alpha = 127 + delta;
            if (alpha < 0)
                alpha = 0;
            if (alpha > 255)
                alpha = 255;
            if (alpha > 0) {
                if (alpha == 255) {
                    put_pixel(xc + x, yc + y, color);
                    if (x != 0)
                        put_pixel(xc - x, yc + y, color);
                } else {
                    put_pixel_AA(xc + x, yc + y, color, (uint8_t)alpha);
                    if (x != 0)
                        put_pixel_AA(xc - x, yc + y, color, (uint8_t)alpha);
                }
            }
        }
    }
}

void shadow_draw(int x, int y, int w, int h) {
    if (!shadow_corner)
        return;
    int r = shadow_radius;
    int sz = shadow_size;
    int dim = r + sz;
    for (int dy = 0; dy < dim; dy++) {
        for (int dx = 0; dx < dim; dx++) {
            uint8_t a = shadow_corner[dy * dim + dx];
            if (a == 0)
                continue;
            plot_pixel(x + r - 1 - dx, y + r - 1 - dy, a);
            plot_pixel(x + w - r + dx, y + r - 1 - dy, a);
            plot_pixel(x + r - 1 - dx, y + h - r + dy, a);
            plot_pixel(x + w - r + dx, y + h - r + dy, a);
        }
    }
    for (int iy = 0; iy < sz; iy++) {
        uint8_t a = shadow_edge[iy];
        if (a == 0)
            continue;
        int py_top = y - 1 - iy;
        int py_bot = y + h + iy;
        for (int ix = r; ix < w - r; ix++) {
            plot_pixel(x + ix, py_top, a);
            plot_pixel(x + ix, py_bot, a);
        }
    }
    for (int ix = 0; ix < sz; ix++) {
        uint8_t a = shadow_edge[ix];
        if (a == 0)
            continue;
        int px_left = x - 1 - ix;
        int px_right = x + w + ix;
        for (int iy = r; iy < h - r; iy++) {
            plot_pixel(px_left, y + iy, a);
            plot_pixel(px_right, y + iy, a);
        }
    }
}

void clear_screen(uint32_t color) {
    if (!back_buffer)
        return;
    memset32(back_buffer, color, fb_width * fb_height);
}

void swap_rect(int x, int y, int w, int h) {
    if (!fb_ptr || !back_buffer) return;
    
    int x1 = x;
    int y1 = y;
    int x2 = x + w;
    int y2 = y + h;

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    
    
    if (x2 > (int)fb_width) x2 = (int)fb_width;
    if (y2 > (int)fb_height) y2 = (int)fb_height;

    if (x1 >= x2 || y1 >= y2) return;

    int new_w = x2 - x1;
    int new_h = y2 - y1;

    for (int i = 0; i < new_h; i++) {
        if (y1 + i >= (int)fb_height) break;

        uint32_t offset = ((y1 + i) * fb_pitch) + (x1 * 4);
        
        memcpy32((void *)(uint8_t *)fb_ptr + offset, (void *)(back_buffer + offset), new_w * 4);
    }
}

void swap_buffers() {
    if (!fb_ptr || !back_buffer)
        return;
    memcpy32(fb_ptr, back_buffer, fb_size_bytes);
}

void char_draw(int x, int y, char c, uint32_t color, uint32_t bgcolor) {
    if (c == 0)
        return;
    const uint8_t *glyph = &font[(int)(unsigned char)c * 16];
    for (int row = 0; row < 16; row++) {
        uint8_t data = glyph[row];
        for (int col = 0; col < 8; col++) {
            if ((data >> (7 - col)) & 1)
                put_pixel(x + col, y + row, color);
            else
                put_pixel(x + col, y + row, bgcolor);
        }
    }
}

void string_draw(int x, int y, const char *str, uint32_t color, uint32_t bgcolor) {
    while (*str) {
        char_draw(x, y, *str++, color, bgcolor);
        x += 8;
    }
}

void draw_sprite(int x, int y, int w, int h, const uint8_t *data, const uint32_t *palette) {
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            if (x + j < 0 || x + j >= (int)fb_width || y + i < 0 || y + i >= (int)fb_height)
                continue;
            uint8_t colorIndex = data[i * w + j];
            if (colorIndex == 0)
                continue;
            uint32_t color = palette[colorIndex];
            put_pixel(x + j, y + i, color);
        }
    }
}

void draw_corner_mask(int x, int y, int r, uint32_t color, const uint8_t *mask, int quadrant) {
    if (!back_buffer || !mask)
        return;
    for (int dy = 0; dy < r; dy++) {
        for (int dx = 0; dx < r; dx++) {
            int screen_x = x + dx;
            int screen_y = y + dy;
            if (screen_x < 0 || screen_x >= (int)fb_width || screen_y < 0 || screen_y >= (int)fb_height)
                continue;
            int mask_x, mask_y;
            switch (quadrant) {
            case 0:
                mask_x = dx;
                mask_y = dy;
                break;
            case 1:
                mask_x = (r - 1) - dx;
                mask_y = dy;
                break;
            case 2:
                mask_x = dx;
                mask_y = (r - 1) - dy;
                break;
            case 3:
                mask_x = (r - 1) - dx;
                mask_y = (r - 1) - dy;
                break;
            default:
                return;
            }
            uint8_t alpha = mask[mask_y * r + mask_x];
            if (alpha == 255) {
                put_pixel(screen_x, screen_y, color);
            } else if (alpha > 0) {
                put_pixel_AA(screen_x, screen_y, color, alpha);
            }
        }
    }
}