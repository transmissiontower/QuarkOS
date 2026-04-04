#include "gfx/bmp.h"
#include "drivers/GUI/framebuffer.h"

void draw_bmp(void *bmp_data, int x, int y)
{
    BITMAPFILEHEADER *file_header = (BITMAPFILEHEADER *)bmp_data;
    BITMAPINFOHEADER *info_header = (BITMAPINFOHEADER *)(bmp_data + sizeof(BITMAPFILEHEADER));

    uint8_t *pixels = (uint8_t *)bmp_data + file_header->bf_off_bits;
    int width = info_header->bi_width;
    int height = info_header->bi_height;
    int bpp = info_header->bi_bit_count;
    int bytes_per_pixel = bpp / 8;

    int stride = ((width * bpp + 31) / 32) * 4;

    for (int i = 0; i < height; i++) {
        uint8_t *row = pixels + (i * stride);

        for (int j = 0; j < width; j++) {
            uint8_t *pixel_ptr = row + (j * bytes_per_pixel);

            uint8_t b = pixel_ptr[0];
            uint8_t g = pixel_ptr[1];
            uint8_t r = pixel_ptr[2];
            uint8_t a = (bpp == 32) ? pixel_ptr[3] : 255;

            if (a == 0)
                continue;

            int screen_x = x + j;
            int screen_y = y + (height - i - 1);

            uint32_t color = (r << 16) | (g << 8) | b;

            if (a == 255) {
                put_pixel(screen_x, screen_y, color);
            } else {
                put_pixel_AA(screen_x, screen_y, color, a);
            }
        }
    }
}