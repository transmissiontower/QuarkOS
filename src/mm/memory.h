#ifndef MEMORY_H
#define MEMORY_H
#include "arch/x86/multiboot.h"
#include "lib/stdint.h"
#define KERNEL_START 0xC0000000
#define KERNEL_MALLOC 0xD0000000
#define MODULE_VIRT_START 0xD2000000
#define MODULE_VIRT_END 0xE0000000
#define REC_PAGEDIR ((uint32_t *)0xFFFFF000)
#define Rec_PAGEDIR(i) ((uint32_t *)(0xFFC00000 + ((i) << 12)))
#define PAGE_FLAG_PRESENT (1 << 0)
#define PAGE_FLAG_WRITE (1 << 1)
#define PAGE_FLAG_USER (1 << 2)
#define PAGE_FLAG_OWNER  (1 << 9)
#define PAGE_FLAG_SHARED (1 << 10)
#define MAX_PHYS_PAGES 1048576
#define BITMAP_SIZE (MAX_PHYS_PAGES / 8)
extern uint32_t initial_page_dir[1024];
void memory_init(struct multiboot_info *mboot, uint32_t physAllocStart);
void mm_vmm_tlb_flush_single(uint32_t vaddr);
uint32_t mm_pmm_alloc_frame();
uint32_t *mem_get_current_page_dir();
void mm_pmm_free_frame(uint32_t phys);
void mm_vmm_cleanup_current_dir();
void mm_vmm_set_active_dir(uint32_t *pd);
void mm_vmm_map_page(uint32_t virtualAddr, uint32_t physAddr, uint32_t flags);
void kernel_panic(const char *msg);
void *map_module(uint32_t phys_addr, uint32_t size);
void mem_free_identity_map();
#endif
