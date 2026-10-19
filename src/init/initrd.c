// SPDX-License-Identifier: MIT
#include "kernel/bootinfo.h"
#include "kernel/process.h"
#include "init/init.h"
#include "arch/aarch64/platform.h"
#include "lib/stdlib.h"
#include "mm/memory.h"
#include "util/util.h"

#define QEX_MAGIC 0x5845517FU
#define FONT_MAGIC 0x464F4E54U

extern void mm_vmm_set_l1_override(uint64_t *l1);
extern void mm_vmm_clear_l1_override(void);

uint32_t k_font_size;
uint32_t k_font_phys_addr;
void *k_font_virt_addr;
uint32_t k_icons_phys_start;
uint32_t k_icons_size;
uint32_t k_wallpaper_phys_addr;
uint32_t k_wallpaper_size;
uint32_t k_logo_phys_addr;
uint32_t k_logo_size;

static int module_name_is(const char *cmdline, const char *name) {
    if (!cmdline)
        return 0;
    while (*cmdline == ' ' || *cmdline == '"')
        cmdline++;
    while (*name && *cmdline == *name) {
        cmdline++;
        name++;
    }
    return !*name && (!*cmdline || *cmdline == '.' || *cmdline == ' ' || *cmdline == '"');
}

static void map_user_region(uint32_t virt, uint32_t phys, uint32_t size, uint32_t flags) {
    uint32_t offset = phys & 0xFFFU;
    uint32_t phys_page = phys & ~0xFFFU;
    uint32_t pages = (size + offset + 0xFFFU) / 4096U;
    for (uint32_t i = 0; i < pages; i++)
        mm_vmm_map_page(virt + i * 4096U, phys_page + i * 4096U, flags);
}

void k_start_init_process(const boot_info_t *boot_info) {
    if (!boot_info || !boot_info->modules_addr || boot_info->module_count == 0 ||
        boot_info->module_count > 12)
        kernel_panic("init: invalid module list");

    boot_module_t *mods = (boot_module_t *)(uintptr_t)boot_info->modules_addr;
    uint32_t init_size = 0;
    uint32_t initrd_phys = boot_info->initrd_phys_addr;
    uint32_t initrd_size = boot_info->initrd_size;
    void *init_image = 0;

    for (uint32_t i = 0; i < boot_info->module_count; i++) {
        if (mods[i].end <= mods[i].start)
            continue;
        uint32_t size = mods[i].end - mods[i].start;
        void *virt = map_module(mods[i].start, size);
        if (!virt || size < sizeof(uint32_t))
            continue;
        const char *cmdline = (const char *)(uintptr_t)mods[i].command_line;

        if (cmdline && strstr(cmdline, "type=wallpaper")) {
            k_wallpaper_phys_addr = mods[i].start;
            k_wallpaper_size = size;
        } else if (cmdline && strstr(cmdline, "type=icons")) {
            k_icons_phys_start = mods[i].start;
            k_icons_size = size;
        } else if (cmdline && strstr(cmdline, "type=logo")) {
            k_logo_phys_addr = mods[i].start;
            k_logo_size = size;
        } else if (*(uint32_t *)virt == FONT_MAGIC) {
            k_font_phys_addr = mods[i].start;
            k_font_size = size;
            k_font_virt_addr = virt;
        } else if (*(uint32_t *)virt == QEX_MAGIC && module_name_is(cmdline, "init")) {
            init_image = virt;
            init_size = size;
        }
    }

    if (!init_image || !initrd_phys || initrd_size < SODIUM_CPIO_NEWC_HEADER_SIZE)
        kernel_panic("init: executable or initrd missing");
    if ((uint64_t)initrd_size + (initrd_phys & 0xFFFU) >
        SODIUM_INITRD_LIMIT - SODIUM_INITRD_ADDRESS)
        kernel_panic("init: initrd exceeds user address space");

    process_t *init = k_proc_spawn((uint8_t *)init_image, init_size);
    if (!init)
        kernel_panic("init: cannot start PID 1");
    strncpy(init->name, "init", sizeof(init->name) - 1);

    uint32_t context_phys = mm_pmm_alloc_frame();
    if (!context_phys)
        kernel_panic("init: cannot allocate startup context");
    sodium_init_context_t *context =
        (sodium_init_context_t *)(uintptr_t)(context_phys + 0x80000000U);
    context->address = SODIUM_INITRD_ADDRESS + (initrd_phys & 0xFFFU);
    context->size = initrd_size;

    mm_vmm_set_l1_override((uint64_t *)(uintptr_t)(init->pd_phys + 0x80000000U));
    map_user_region(SODIUM_INIT_CONTEXT_ADDRESS, context_phys, 4096,
                    PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE | PAGE_FLAG_USER);
    map_user_region(SODIUM_INITRD_ADDRESS, initrd_phys, initrd_size,
                    PAGE_FLAG_PRESENT | PAGE_FLAG_USER);
    mm_vmm_clear_l1_override();
    arm_dsb();
    arm_tlb_flush_all();
}
