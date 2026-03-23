#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H

#include "lib/stdint.h"

typedef struct {
    uint32_t entry;
    uint32_t image_base;
    uint32_t image_end;
} exec_image_info_t;

int elf_validate(const uint8_t *data, uint32_t size);
int elf_load_image(const uint8_t *data, uint32_t size, exec_image_info_t *info);

#endif
