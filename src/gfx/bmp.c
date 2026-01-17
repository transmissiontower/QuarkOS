#include "gfx/bmp.h"
#include "mm/kmalloc.h"
#include "util/util.h"
#include "video/texture.h"

Texture *load_bmp(uint8_t *raw_data) {
    BITMAPFILEHEADER *file_header = (BITMAPFILEHEADER *)raw_data;

    if (file_header->bf_type != 0x4D42) {
        return 0;
    }

    BITMAPINFOHEADER *info_header = (BITMAPINFOHEADER *)(raw_data + sizeof(BITMAPFILEHEADER));

    int w = info_header->bi_width;
    int h = info_header->bi_height;
    int bpp = info_header->bi_bit_count;

    if (bpp != 24 && bpp != 32) {
        return 0;
    }

    int flip = 1;
    if (h < 0) {
        h = -h;
        flip = 0;
    }

    Texture *tex = create_texture(w, h);
    if (!tex)
        return 0;

    uint8_t *pixels = raw_data + file_header->bf_off_bits;

    int bytes_per_pixel = bpp / 8;
    int pitch = (w * bytes_per_pixel + 3) & (~3);

    for (int y = 0; y < h; y++) {
        int src_y = flip ? (h - 1 - y) : y;

        uint8_t *src_row = pixels + (src_y * pitch);

        for (int x = 0; x < w; x++) {
            uint8_t *px = src_row + (x * bytes_per_pixel);

            uint8_t b = px[0];
            uint8_t g = px[1];
            uint8_t r = px[2];
            uint8_t a = 255;

            if (bpp == 32) {
                a = px[3];
            }

            uint32_t color = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;

            tex->data[y * w + x] = color;
        }
    }

    return tex;
}