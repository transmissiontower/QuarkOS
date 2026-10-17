#ifndef KERNEL_BOOTINFO_H
#define KERNEL_BOOTINFO_H

#include "lib/stdint.h"

typedef struct {
    uint32_t start;
    uint32_t end;
    uint32_t command_line;
} boot_module_t;

typedef struct {
    uint32_t ram_size;
    uint32_t module_count;
    uint32_t modules_addr;
    uint32_t framebuffer_addr;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_bpp;
} boot_info_t;

#endif
