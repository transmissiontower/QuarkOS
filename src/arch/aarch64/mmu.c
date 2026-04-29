// SPDX-License-Identifier: MIT
/*
 * src/arch/aarch64/mmu.c - AArch64 MMU page table management and physical memory allocator
 */
#include "mm/memory.h"
#include "arch/aarch64/platform.h"
#include "kernel/crash.h"
#include "kernel/klog.h"
#include "lib/stdint.h"
#include "util/util.h"
#include "video/vga.h"
#include "drivers/GUI/framebuffer.h"

#define PHYS_TO_VIRT(p) ((uint32_t)(p) + 0x80000000U)
#define VIRT_TO_PHYS(v) ((uint32_t)(v) - 0x80000000U)

#define DESC_FAULT      0x0ULL
#define DESC_BLOCK      0x1ULL
#define DESC_TABLE      0x3ULL
#define DESC_PAGE       0x3ULL

#define L3_VALID        (1ULL << 0)
#define L3_PAGE         (1ULL << 1)
#define L3_AF           (1ULL << 10)

#define L3_ATTR_NORMAL  (0ULL << 2)
#define L3_ATTR_DEVICE  (1ULL << 2)

#define L3_AP_KERN_RW   (0ULL << 6)
#define L3_AP_FULL      (1ULL << 6)

#define L3_SH_INNER     (3ULL << 8)

static uint8_t phys_mem_bitmap[BITMAP_SIZE];
static uint32_t page_frame_min;
static uint32_t page_frame_max;
uint32_t total_alloc;
uint32_t total_phys_pages;
static uint32_t last_search_index = 0;
int mem_num_vpages;

#define REFCOUNT_PAGES (ARM_RAM_SIZE / 4096)
static uint8_t page_refcount[REFCOUNT_PAGES];

void mm_page_ref_inc(uint32_t phys)
{
    uint32_t idx = (phys - ARM_PHYS_OFFSET) >> 12;
    if (idx < REFCOUNT_PAGES && page_refcount[idx] < 255)
        page_refcount[idx]++;
}

int mm_page_ref_dec(uint32_t phys)
{
    uint32_t idx = (phys - ARM_PHYS_OFFSET) >> 12;
    if (idx >= REFCOUNT_PAGES)
        return 1;
    if (page_refcount[idx] == 0)
        return 1;
    page_refcount[idx]--;
    return (page_refcount[idx] == 0) ? 1 : 0;
}

uint8_t mm_page_ref_get(uint32_t phys)
{
    uint32_t idx = (phys - ARM_PHYS_OFFSET) >> 12;
    if (idx >= REFCOUNT_PAGES) return 0;
    return page_refcount[idx];
}

static uint32_t module_virt_alloc_ptr = MODULE_VIRT_START;

static uint64_t *override_l1 = 0;
void mm_vmm_set_l1_override(uint64_t *l1) { override_l1 = l1; }
void mm_vmm_clear_l1_override(void) { override_l1 = 0; }

static inline void pte_clean(volatile void *addr)
{
    asm volatile("dc civac, %0" :: "r"((uint64_t)(uintptr_t)addr) : "memory");
    asm volatile("dsb sy" ::: "memory");
}

static inline void page_clean(void *addr)
{
    uint64_t base = (uint64_t)(uintptr_t)addr;
    for (uint64_t off = 0; off < 4096; off += 64)
        asm volatile("dc civac, %0" :: "r"(base + off) : "memory");
    asm volatile("dsb sy" ::: "memory");
}

extern uint64_t aarch64_kernel_l1[];

uint32_t *initial_page_dir_ptr;

void kernel_panic(const char *msg)
{
    arm_irq_disable();
    clear_screen(COLOR_RED);
    string_draw(20, 20, "KERNEL PANIC", COLOR_WHITE, COLOR_RED);
    if (msg && *msg)
        string_draw(20, 50, msg, COLOR_WHITE, COLOR_RED);
    string_draw(20, 90, "System halted", COLOR_WHITE, COLOR_RED);
    drv_fb_swap_buffers();
    while (1) arm_wfi();
}

static void phex(uint32_t v) {
    for (int s=28;s>=0;s-=4){int n=(v>>s)&0xF;arm_uart_putc(n<10?'0'+n:'A'+n-10);}
}
static void phex64(uint64_t v) {
    phex((uint32_t)(v>>32)); phex((uint32_t)v);
}

void debug_print_hex32(uint64_t v) { phex((uint32_t)v); }
void debug_print_hex64(uint64_t v) { phex64(v); }

volatile int hvf_ttbr0_changed = 0;

void hvf_sync_pagetables(void)
{

    if (!hvf_ttbr0_changed) {
        (void)mmio_read32(GIC_CPU_VIRT + GICC_PMR);
        (void)mmio_read32(GIC_CPU_VIRT + GICC_PMR);
        arm_dsb();
        return;
    }

    for (int i = 0; i < 4; i++) {
        (void)mmio_read32(GIC_CPU_VIRT + GICC_PMR);
        (void)mmio_read32(GIC_CPU_VIRT + GICC_PMR);
        (void)mmio_read32(GIC_CPU_VIRT + GICC_PMR);
        (void)mmio_read32(GIC_CPU_VIRT + GICC_PMR);
        arm_dsb();
    }
    arm_tlb_flush_all();
    hvf_ttbr0_changed = 0;
}

static int debug_irq_count = 0;
void debug_check_el0_return(uint64_t elr)
{

    if (debug_irq_count >= 10) return;
    debug_irq_count++;

    uint64_t ttbr0;
    asm volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));

    asm volatile("at s1e0r, %0" :: "r"(elr));
    asm volatile("isb" ::: "memory");
    uint64_t par;
    asm volatile("mrs %0, PAR_EL1" : "=r"(par));

    arm_uart_puts("IRQ_RET: elr=0x");
    phex((uint32_t)elr);
    arm_uart_puts(" ttbr0=0x");
    phex((uint32_t)ttbr0);
    arm_uart_puts(" par=0x");
    phex64(par);
    if (par & 1) arm_uart_puts(" FAIL");
    arm_uart_putc('\n');
}

void debug_dump_pt_walk(uint64_t entry_va)
{
    uint64_t ttbr0;
    asm volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));
    uint32_t ttbr0_32 = (uint32_t)ttbr0;

    arm_uart_puts("PT_WALK: entry=0x"); phex((uint32_t)entry_va);
    arm_uart_puts(" TTBR0=0x"); phex(ttbr0_32);

    uint64_t *l1 = (uint64_t *)(uintptr_t)PHYS_TO_VIRT(ttbr0_32);
    uint32_t l1_idx = ((uint32_t)entry_va >> 30) & 0x1FF;
    uint64_t l1e = l1[l1_idx];
    arm_uart_puts(" L1["); phex(l1_idx); arm_uart_puts("]=0x"); phex64(l1e);

    if ((l1e & 0x3) != 0x3) {
        arm_uart_puts(" FAULT!\n");
        return;
    }

    uint32_t l2_phys = (uint32_t)(l1e & 0xFFFFF000ULL);
    uint64_t *l2 = (uint64_t *)(uintptr_t)PHYS_TO_VIRT(l2_phys);
    uint32_t l2_idx = ((uint32_t)entry_va >> 21) & 0x1FF;
    uint64_t l2e = l2[l2_idx];
    arm_uart_puts(" L2["); phex(l2_idx); arm_uart_puts("]=0x"); phex64(l2e);

    if ((l2e & 0x3) != 0x3) {
        arm_uart_puts(" FAULT!\n");
        return;
    }

    uint32_t l3_phys = (uint32_t)(l2e & 0xFFFFF000ULL);
    uint64_t *l3 = (uint64_t *)(uintptr_t)PHYS_TO_VIRT(l3_phys);
    uint32_t l3_idx = ((uint32_t)entry_va >> 12) & 0x1FF;
    uint64_t l3e = l3[l3_idx];
    arm_uart_puts(" L3["); phex(l3_idx); arm_uart_puts("]=0x"); phex64(l3e);
    arm_uart_puts(" OK\n");
}

void mm_vmm_tlb_flush_single(uint32_t addr)
{
    arm_tlb_flush_single(addr);
}

void assert_valid_kernel_ptr(const void *ptr, const char *context)
{
    uintptr_t addr = (uintptr_t)ptr;
    if (!ptr) {
        KLOG_VMM(LOG_CRIT, "NULL kernel pointer in %s", context);
        kernel_panic("NULL kernel pointer dereference");
    }
    if (addr < KERNEL_START || addr > 0xFFFFFFFF) {
        KLOG_VMM(LOG_CRIT, "invalid kernel ptr %p in %s (not in 0x%x-0xFFFFFFFF)",
                 ptr, context, KERNEL_START);
        kernel_panic("invalid kernel pointer");
    }
}

uint32_t mm_virt_to_phys(uint32_t virt)
{
    uint64_t ttbr0;
    asm volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));
    uint32_t ttbr0_32 = (uint32_t)ttbr0;

    uint64_t *l1 = (uint64_t *)(uintptr_t)PHYS_TO_VIRT(ttbr0_32);
    uint32_t l1_idx = (virt >> 30) & 0x1FF;
    uint64_t l1e = l1[l1_idx];
    if ((l1e & 0x3) != 0x3)
        return 0;

    uint32_t l2_phys = (uint32_t)(l1e & 0xFFFFF000ULL);
    uint64_t *l2 = (uint64_t *)(uintptr_t)PHYS_TO_VIRT(l2_phys);
    uint32_t l2_idx = (virt >> 21) & 0x1FF;
    uint64_t l2e = l2[l2_idx];
    if ((l2e & 0x3) != 0x3)
        return 0;

    uint32_t l3_phys = (uint32_t)(l2e & 0xFFFFF000ULL);
    uint64_t *l3 = (uint64_t *)(uintptr_t)PHYS_TO_VIRT(l3_phys);
    uint32_t l3_idx = (virt >> 12) & 0x1FF;
    uint64_t l3e = l3[l3_idx];
    if (!(l3e & 0x1))
        return 0;

    return (uint32_t)(l3e & 0xFFFFF000ULL) | (virt & 0xFFF);
}

uint32_t *mem_get_current_page_dir(void)
{
    uint32_t ttbr0 = arm_get_ttbr0();
    return (uint32_t *)((uintptr_t)PHYS_TO_VIRT(ttbr0));
}

void mm_vmm_set_active_dir(uint32_t *pd)
{
    uint32_t phys = VIRT_TO_PHYS((uint32_t)(uintptr_t)pd);
    arm_set_ttbr0(phys);
    arm_tlb_flush_all();
}

void mm_pmm_lock_region(uint32_t base, uint32_t size)
{
    uint32_t start = base >> 12;
    uint32_t count = (size + 0xFFF) >> 12;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t page = start + i;
        if (page >= MAX_PHYS_PAGES) break;
        phys_mem_bitmap[page >> 3] |= (1 << (page & 7));
    }
}

void mm_pmm_free_region(uint32_t base, uint32_t size)
{
    uint32_t start = (base + 0xFFF) >> 12;
    uint32_t end = (base + size) >> 12;
    for (uint32_t page = start; page < end; page++) {
        if (page >= MAX_PHYS_PAGES) break;
        phys_mem_bitmap[page >> 3] &= ~(1 << (page & 7));
        if (page > page_frame_max) page_frame_max = page;
    }
}

uint32_t mm_pmm_alloc_frame(void)
{
    uint32_t start_byte = page_frame_min >> 3;
    uint32_t end_byte = (page_frame_max >> 3) + 1;
    if (end_byte > BITMAP_SIZE) end_byte = BITMAP_SIZE;
    uint32_t current_byte = last_search_index;
    if (current_byte < start_byte || current_byte >= end_byte)
        current_byte = start_byte;
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

void mm_pmm_free_frame(uint32_t phys)
{
    uint32_t page = phys >> 12;
    if (page >= MAX_PHYS_PAGES) {
        KLOG_PMM(LOG_ERROR, "free out of range: phys=%x page=%u", phys, page);
        return;
    }
    if (!(phys_mem_bitmap[page >> 3] & (1 << (page & 7)))) {
        KLOG_PMM(LOG_WARN, "double-free detected: phys=%x (already free)", phys);
        return;
    }
    phys_mem_bitmap[page >> 3] &= ~(1 << (page & 7));
    if (total_alloc > 0) total_alloc--;
}

uint32_t mm_pmm_alloc_l1(void)
{
    return mm_pmm_alloc_frame();
}

void mm_vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags)
{
    uint32_t l1_idx = (virt >> 30) & 0x1FF;
    uint32_t l2_idx = (virt >> 21) & 0x1FF;
    uint32_t l3_idx = (virt >> 12) & 0x1FF;

    uint64_t *l1;
    if (override_l1)
        l1 = override_l1;
    else {
        uint32_t ttbr0 = arm_get_ttbr0();
        l1 = (uint64_t *)((uintptr_t)PHYS_TO_VIRT(ttbr0));
    }

    if ((l1[l1_idx] & 0x3) != DESC_TABLE) {
        uint32_t l2_phys = mm_pmm_alloc_frame();
        if (!l2_phys) {
            KLOG_VMM(LOG_ERROR, "OOM allocating L2 for virt=%x", virt);
            kernel_panic("OOM: L2 alloc");
            return;
        }
        uint64_t *l2 = (uint64_t *)((uintptr_t)PHYS_TO_VIRT(l2_phys));
        memset(l2, 0, 4096);
        page_clean(l2);
        arm_dsb();

        {
            extern void hvf_sync_pagetables(void);
            hvf_sync_pagetables();
        }
        l1[l1_idx] = (uint64_t)l2_phys | DESC_TABLE;
        pte_clean(&l1[l1_idx]);
        arm_tlb_flush_all();

        if (virt >= KERNEL_START) {
            uint64_t *master = (uint64_t *)((uintptr_t)PHYS_TO_VIRT(
                VIRT_TO_PHYS((uint32_t)(uintptr_t)aarch64_kernel_l1)));
            master[l1_idx] = l1[l1_idx];
            pte_clean(&master[l1_idx]);
            arm_tlb_flush_all();
        }
    }

    uint32_t l2_phys = (uint32_t)(l1[l1_idx] & 0xFFFFF000ULL);
    uint64_t *l2 = (uint64_t *)((uintptr_t)PHYS_TO_VIRT(l2_phys));

    if ((l2[l2_idx] & 0x3) == DESC_BLOCK) {
        uint64_t block_pa = l2[l2_idx] & 0xFFFFE00000ULL;
        uint32_t l3_phys_split = mm_pmm_alloc_frame();
        if (!l3_phys_split) {
            KLOG_VMM(LOG_ERROR, "OOM splitting L2 block for virt=%x", virt);
            kernel_panic("OOM: L3 alloc for block split");
            return;
        }
        uint64_t *l3 = (uint64_t *)((uintptr_t)PHYS_TO_VIRT(l3_phys_split));

        memset(l3, 0, 4096);
        page_clean(l3);
        arm_dsb();

        for (int i = 0; i < 512; i++) {
            l3[i] = (block_pa + (uint64_t)i * 4096) | L3_VALID | L3_PAGE |
                     L3_AF | L3_ATTR_NORMAL | L3_AP_KERN_RW | L3_SH_INNER;
        }

        page_clean(l3);
        arm_dsb();
        extern void hvf_sync_pagetables(void);
        hvf_sync_pagetables();

        l2[l2_idx] = (uint64_t)l3_phys_split | DESC_TABLE;
        pte_clean(&l2[l2_idx]);
        arm_dsb();
        hvf_sync_pagetables();
        arm_tlb_flush_all();
    }

    if ((l2[l2_idx] & 0x3) != DESC_TABLE) {
        uint32_t l3_phys_new = mm_pmm_alloc_frame();
        if (!l3_phys_new) {
            KLOG_VMM(LOG_ERROR, "OOM allocating L3 for virt=%x", virt);
            kernel_panic("OOM: L3 alloc");
            return;
        }
        uint64_t *l3 = (uint64_t *)((uintptr_t)PHYS_TO_VIRT(l3_phys_new));
        memset(l3, 0, 4096);
        page_clean(l3);
        arm_dsb();

        {
            extern void hvf_sync_pagetables(void);
            hvf_sync_pagetables();
        }
        l2[l2_idx] = (uint64_t)l3_phys_new | DESC_TABLE;
        pte_clean(&l2[l2_idx]);
        arm_tlb_flush_all();

        if (virt >= KERNEL_START) {
            uint64_t *master_l1 = (uint64_t *)((uintptr_t)PHYS_TO_VIRT(
                VIRT_TO_PHYS((uint32_t)(uintptr_t)aarch64_kernel_l1)));
            uint32_t master_l2_phys = (uint32_t)(master_l1[l1_idx] & 0xFFFFF000ULL);
            uint64_t *master_l2 = (uint64_t *)((uintptr_t)PHYS_TO_VIRT(master_l2_phys));
            master_l2[l2_idx] = l2[l2_idx];
            pte_clean(&master_l2[l2_idx]);
            arm_tlb_flush_all();
        }
    }

    uint32_t l3_phys = (uint32_t)(l2[l2_idx] & 0xFFFFF000ULL);
    uint64_t *l3 = (uint64_t *)((uintptr_t)PHYS_TO_VIRT(l3_phys));

    uint64_t attr = L3_ATTR_NORMAL;
    if (flags & PAGE_FLAG_UNCACHED)
        attr = L3_ATTR_DEVICE;

    uint64_t ap = L3_AP_KERN_RW;
    if (flags & PAGE_FLAG_USER)
        ap = L3_AP_FULL;

    l3[l3_idx] = ((uint64_t)phys & 0xFFFFF000ULL) | L3_VALID | L3_PAGE |
                 L3_AF | attr | ap | L3_SH_INNER;
    pte_clean(&l3[l3_idx]);

    mem_num_vpages++;
    arm_tlb_flush_single(virt);
}

void mm_vmm_unmap_page(uint32_t virt)
{
    uint32_t l1_idx = (virt >> 30) & 0x1FF;
    uint32_t l2_idx = (virt >> 21) & 0x1FF;
    uint32_t l3_idx = (virt >> 12) & 0x1FF;

    uint64_t *l1;
    if (override_l1)
        l1 = override_l1;
    else {
        uint32_t ttbr0 = arm_get_ttbr0();
        l1 = (uint64_t *)((uintptr_t)PHYS_TO_VIRT(ttbr0));
    }

    if ((l1[l1_idx] & 0x3) != DESC_TABLE)
        return;

    uint32_t l2_phys = (uint32_t)(l1[l1_idx] & 0xFFFFF000ULL);
    uint64_t *l2 = (uint64_t *)((uintptr_t)PHYS_TO_VIRT(l2_phys));

    if ((l2[l2_idx] & 0x3) != DESC_TABLE)
        return;

    uint32_t l3_phys = (uint32_t)(l2[l2_idx] & 0xFFFFF000ULL);
    uint64_t *l3 = (uint64_t *)((uintptr_t)PHYS_TO_VIRT(l3_phys));

    l3[l3_idx] = DESC_FAULT;
    pte_clean(&l3[l3_idx]);
    arm_tlb_flush_single(virt);
    if (mem_num_vpages > 0) mem_num_vpages--;
}

void mm_vmm_cleanup_current_dir(void)
{
    uint32_t ttbr0 = arm_get_ttbr0();
    uint64_t *l1 = (uint64_t *)((uintptr_t)PHYS_TO_VIRT(ttbr0));

    for (uint32_t i = 0; i < 3; i++) {
        if ((l1[i] & 0x3) != DESC_TABLE)
            continue;

        uint32_t l2_phys = (uint32_t)(l1[i] & 0xFFFFF000ULL);
        uint64_t *l2 = (uint64_t *)((uintptr_t)PHYS_TO_VIRT(l2_phys));

        for (int j = 0; j < 512; j++) {
            if ((l2[j] & 0x3) == DESC_TABLE) {

                uint32_t l3_phys = (uint32_t)(l2[j] & 0xFFFFF000ULL);
                uint64_t *l3 = (uint64_t *)((uintptr_t)PHYS_TO_VIRT(l3_phys));
                for (int k = 0; k < 512; k++) {
                    if (l3[k] & L3_VALID) {
                        mm_pmm_free_frame((uint32_t)(l3[k] & 0xFFFFF000ULL));
                        l3[k] = 0;
                    }
                }
                mm_pmm_free_frame(l3_phys);
                l2[j] = 0;
            } else if ((l2[j] & 0x3) == DESC_BLOCK) {

                l2[j] = 0;
            }
        }
        mm_pmm_free_frame(l2_phys);
        l1[i] = DESC_FAULT;
    }
    arm_tlb_flush_all();
}

void memory_init(struct multiboot_info *mboot, uint32_t phys_alloc_start)
{
    (void)mboot;
    mem_num_vpages = 0;

    memset(phys_mem_bitmap, 0xFF, sizeof(phys_mem_bitmap));
    memset(page_refcount, 0, sizeof(page_refcount));
    page_frame_min = 0;
    page_frame_max = 0;
    total_alloc = 0;

    uint32_t ram_start = ARM_PHYS_OFFSET;
    uint32_t ram_size = ARM_RAM_SIZE;
    mm_pmm_free_region(ram_start, ram_size);
    total_phys_pages = ram_size / 4096;

    mm_pmm_lock_region(0, phys_alloc_start);
    mm_pmm_lock_region(0x5FF00000, 0x100000);

    page_frame_min = (phys_alloc_start + 0xFFF) >> 12;
    last_search_index = page_frame_min >> 3;

    KLOG_PMM(LOG_INFO, "memory initialized: %u pages free, alloc_start=%x",
             total_phys_pages - total_alloc, phys_alloc_start);
}

void *map_module(uint32_t phys_addr, uint32_t size)
{
    uint32_t virt_addr = module_virt_alloc_ptr;
    if (virt_addr % 4096) virt_addr = (virt_addr & ~0xFFF) + 0x1000;
    uint32_t aligned_size = (size + 0xFFF) & ~0xFFF;
    if (virt_addr + aligned_size > MODULE_VIRT_END) return 0;

    uint32_t start_page = phys_addr & 0xFFFFF000;
    uint32_t end_page = (phys_addr + size - 1) & 0xFFFFF000;
    uint32_t offset = 0;
    for (uint32_t phys = start_page; phys <= end_page; phys += 4096) {
        mm_vmm_map_page(virt_addr + offset, phys, PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE);
        offset += 4096;
    }
    module_virt_alloc_ptr = virt_addr + offset;
    return (void *)(uintptr_t)(virt_addr + (phys_addr & 0xFFF));
}

void mem_free_identity_map(void)
{

    uint64_t *l1 = aarch64_kernel_l1;
    l1[1] = DESC_FAULT;
    arm_tlb_flush_all();
}

void arm_map_high_vectors(void)
{

}
