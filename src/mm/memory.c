// SPDX-License-Identifier: MIT
/*
 * src/mm/memory.c - Physical frame allocator, virtual memory mapper, and kernel panic handler
 */
#include "mm/memory.h"
#include "kernel/multiboot.h"
#include "drivers/GUI/framebuffer.h"
#include "kernel/crash.h"
#include "lib/stdint.h"
#include "util/util.h"
#include "video/vga.h"
#define PDIR_VIRT 0xFFFFF000
#define PTAB_VIRT(i) (0xFFC00000 + ((i) << 12))
static uint32_t module_virt_alloc_ptr = MODULE_VIRT_START;
static uint8_t phys_mem_bitmap[BITMAP_SIZE];
static uint32_t page_frame_min;
static uint32_t page_frame_max;
uint32_t total_alloc;
uint32_t total_phys_pages;
static uint32_t last_search_index = 0;
int mem_num_vpages;
void kernel_panic(const char *msg) {
    asm volatile("cli");
    clear_screen(COLOR_RED);
    string_draw(20, 20, "KERNEL PANIC", COLOR_WHITE, COLOR_RED);
    if (msg && *msg) {
        string_draw(20, 50, msg, COLOR_WHITE, COLOR_RED);
    }
    string_draw(20, 90, "Press R to reboot", COLOR_WHITE, COLOR_RED);
    drv_fb_swap_buffers();
    crash_wait_for_reboot();
}
void mm_vmm_tlb_flush_single(uint32_t addr) {
    asm volatile("invlpg (%0)" ::"r"(addr) : "memory");
}
uint32_t *mem_get_current_page_dir() {
    return REC_PAGEDIR;
}
void mm_vmm_set_active_dir(uint32_t *pd) {
    uint32_t phys = (uint32_t)pd - KERNEL_START;
    asm volatile("mov %0, %%eax\n\t"
                 "mov %%eax, %%cr3\n" ::"r"(phys)
                 : "eax");
}
void mm_pmm_lock_region(uint32_t base, uint32_t size) {
    uint32_t start = base >> 12;
    uint32_t count = (size + 0xFFF) >> 12;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t page = start + i;
        if (page >= MAX_PHYS_PAGES)
            break;
        phys_mem_bitmap[page >> 3] |= (1 << (page & 7));
    }
}
void mm_pmm_free_region(uint32_t base, uint32_t size) {
    uint32_t start = (base + 0xFFF) >> 12;
    uint32_t end = (base + size) >> 12;
    for (uint32_t page = start; page < end; page++) {
        if (page >= MAX_PHYS_PAGES)
            break;
        phys_mem_bitmap[page >> 3] &= ~(1 << (page & 7));
        if (page > page_frame_max)
            page_frame_max = page;
    }
}
uint32_t mm_pmm_alloc_frame() {
    uint32_t start_byte = page_frame_min >> 3;
    uint32_t end_byte = (page_frame_max >> 3) + 1;
    if (end_byte > BITMAP_SIZE)
        end_byte = BITMAP_SIZE;
    uint32_t current_byte = last_search_index;
    if (current_byte < start_byte || current_byte >= end_byte) {
        current_byte = start_byte;
    }
    for (uint32_t b = current_byte; b < end_byte; b++) {
        if (phys_mem_bitmap[b] != 0xFF) {
            for (uint32_t i = 0; i < 8; i++) {
                if (!(phys_mem_bitmap[b] & (1 << i))) {
                    phys_mem_bitmap[b] |= (1 << i);
                    total_alloc++;
                    last_search_index = b;
                    return (b * 8 + i) << 12;
                }
            }
        }
    }
    for (uint32_t b = start_byte; b < current_byte; b++) {
        if (phys_mem_bitmap[b] != 0xFF) {
            for (uint32_t i = 0; i < 8; i++) {
                if (!(phys_mem_bitmap[b] & (1 << i))) {
                    phys_mem_bitmap[b] |= (1 << i);
                    total_alloc++;
                    last_search_index = b;
                    return (b * 8 + i) << 12;
                }
            }
        }
    }
    return 0;
}
void mm_pmm_free_frame(uint32_t phys) {
    uint32_t page = phys >> 12;
    if (page >= MAX_PHYS_PAGES)
        return;
    if (phys_mem_bitmap[page >> 3] & (1 << (page & 7))) {
        phys_mem_bitmap[page >> 3] &= ~(1 << (page & 7));
        if (total_alloc > 0)
            total_alloc--;
    }
}
void mm_vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x3FF;
    uint32_t *pd = REC_PAGEDIR;
    if (!(pd[pd_index] & PAGE_FLAG_PRESENT)) {
        uint32_t pt_phys = mm_pmm_alloc_frame();
        if (!pt_phys)
            kernel_panic("Out of memory");
        uint32_t pde_flags = PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE | PAGE_FLAG_OWNER;
        if (flags & PAGE_FLAG_USER)
            pde_flags |= PAGE_FLAG_USER;
        pd[pd_index] = pt_phys | pde_flags;
        if (virt >= KERNEL_START) {
            initial_page_dir[pd_index] = pt_phys | pde_flags;
        }
        uint32_t *pt = Rec_PAGEDIR(pd_index);
        mm_vmm_tlb_flush_single((uint32_t)pt);
        memset(pt, 0, 4096);
    }
    uint32_t *pt = Rec_PAGEDIR(pd_index);
    pt[pt_index] = phys | PAGE_FLAG_PRESENT | flags;
    mem_num_vpages++;
    mm_vmm_tlb_flush_single(virt);
}
void mm_vmm_cleanup_current_dir() {
    uint32_t *pd = REC_PAGEDIR;
    for (int i = 0; i < 768; i++) {
        if (pd[i] & PAGE_FLAG_PRESENT) {
            uint32_t *pt = Rec_PAGEDIR(i);
            for (int j = 0; j < 1024; j++) {
                if (pt[j] & PAGE_FLAG_PRESENT) {
                    if (!(pt[j] & PAGE_FLAG_SHARED))
                        mm_pmm_free_frame(pt[j] & 0xFFFFF000);
                    pt[j] = 0;
                }
            }
            mm_pmm_free_frame(pd[i] & 0xFFFFF000);
            pd[i] = 0;
            mm_vmm_tlb_flush_single(i << 22);
        }
    }
}
void memory_init(struct multiboot_info *mboot, uint32_t phys_alloc_start) {
    mem_num_vpages = 0;
    uint32_t pd_phys = (uint32_t)initial_page_dir - KERNEL_START;
    initial_page_dir[1023] = pd_phys | PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE;
    mm_vmm_set_active_dir(initial_page_dir);
    memset(phys_mem_bitmap, 0xFF, sizeof(phys_mem_bitmap));
    page_frame_min = 0;
    page_frame_max = 0;
    total_alloc = 0;
    if (mboot->flags & (1 << 6)) {
        struct multiboot_mmap_entry *mmap = (struct multiboot_mmap_entry *)mboot->mmap_addr;
        uint32_t end = mboot->mmap_addr + mboot->mmap_length;
        while ((uint32_t)mmap < end) {
            if (mmap->type == 1 && mmap->addr_high == 0) {
                mm_pmm_free_region(mmap->addr_low, mmap->len_low);
            }
            mmap = (void *)((uint32_t)mmap + mmap->size + sizeof(uint32_t));
        }
    } else {
        mm_pmm_free_region(0x100000, mboot->mem_upper * 1024);
    }
    total_phys_pages = (mboot->mem_upper + 1024) / 4;
    mm_pmm_lock_region(0, phys_alloc_start);
    page_frame_min = (phys_alloc_start + 0xFFF) >> 12;
    last_search_index = page_frame_min >> 3;
}
void *map_module(uint32_t phys_addr, uint32_t size) {
    uint32_t virt_addr = module_virt_alloc_ptr;
    if (virt_addr % 4096)
        virt_addr = (virt_addr & ~0xFFF) + 0x1000;
    uint32_t aligned_size = (size + 0xFFF) & ~0xFFF;
    if (virt_addr + aligned_size > MODULE_VIRT_END || virt_addr + aligned_size < virt_addr)
        return 0;
    uint32_t start_page = phys_addr & 0xFFFFF000;
    uint32_t end_page = (phys_addr + size - 1) & 0xFFFFF000;
    uint32_t offset = 0;
    for (uint32_t phys = start_page; phys <= end_page; phys += 4096) {
        mm_vmm_map_page(virt_addr + offset, phys, 3);
        offset += 4096;
    }
    module_virt_alloc_ptr = virt_addr + offset;
    return (void *)(virt_addr + (phys_addr & 0xFFF));
}
void mem_free_identity_map() {
    initial_page_dir[0] = 0;
    initial_page_dir[1] = 0;
    initial_page_dir[2] = 0;
    initial_page_dir[3] = 0;
    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" ::"r"(cr3));
}
