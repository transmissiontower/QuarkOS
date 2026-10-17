// SPDX-License-Identifier: MIT
#ifndef MEMORY_H
#define MEMORY_H
#include "lib/stdint.h"
#define KERNEL_START 0xC0000000
#define MODULE_VIRT_START 0xD2000000
#define MODULE_VIRT_END 0xE0000000
#define PAGE_FLAG_PRESENT (1 << 0)
#define PAGE_FLAG_WRITE (1 << 1)
#define PAGE_FLAG_USER (1 << 2)
#define PAGE_FLAG_SHARED (1 << 10)
#define PAGE_FLAG_UNCACHED (1 << 11)
#define MAX_PHYS_PAGES 1048576
#define BITMAP_SIZE (MAX_PHYS_PAGES / 8)
void memory_init(uint32_t phys_alloc_start);
void mm_vmm_tlb_flush_single(uint32_t vaddr);
uint32_t mm_pmm_alloc_frame();
void mm_pmm_free_frame(uint32_t phys);
void mm_vmm_cleanup_current_dir();
void mm_vmm_cleanup_dir_phys(uint32_t pd_phys);
void mm_vmm_map_page(uint32_t virtualAddr, uint32_t physAddr, uint32_t flags);
void kernel_panic(const char *msg);
void *map_module(uint32_t phys_addr, uint32_t size);
uint32_t mm_virt_to_phys(uint32_t virt);

void mm_page_ref_inc(uint32_t phys);
int mm_page_ref_dec(uint32_t phys);
uint8_t mm_page_ref_get(uint32_t phys);

void mm_vmm_unmap_page(uint32_t virt);
#endif
