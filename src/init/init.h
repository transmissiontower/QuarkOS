// SPDX-License-Identifier: MIT
#ifndef INIT_INIT_H
#define INIT_INIT_H

#include "lib/stdint.h"

struct boot_info;

#define SODIUM_INITRD_ADDRESS 0x90000000U
#define SODIUM_INIT_CONTEXT_ADDRESS 0x8F000000U
#define SODIUM_INITRD_LIMIT 0xA0000000U
#define SODIUM_CPIO_NEWC_HEADER_SIZE 110U

typedef struct {
    uint32_t address;
    uint32_t size;
} sodium_init_context_t;

void k_start_init_process(const struct boot_info *boot_info);

#endif
