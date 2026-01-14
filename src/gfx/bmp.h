#ifndef BMP_H
#define BMP_H

#include "lib/stdint.h"
#include "video/texture.h"

#pragma pack(push, 1)

typedef struct {
    uint16_t bf_type;
    uint32_t bf_size;
    uint16_t bf_reserved1;
    uint16_t bf_reserved2;
    uint32_t bf_off_bits;
} BITMAPFILEHEADER;

typedef struct {
    uint32_t biSize;
    int32_t bi_width;
    int32_t bi_height;
    uint16_t bi_planes;
    uint16_t bi_bit_count;
    uint32_t bi_compression;
    uint32_t bi_size_image;
    int32_t bi_xpels_per_meter;
    int32_t bi_ypels_per_meter;
    uint32_t bi_clr_used;
    uint32_t bi_clr_important;
} BITMAPINFOHEADER;

Texture *load_bmp(uint8_t *fileData);

#pragma pack(pop)

#endif