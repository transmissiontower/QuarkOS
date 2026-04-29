// SPDX-License-Identifier: MIT
/*
 * src/arch/aarch64/platform.c - AArch64 virt-machine platform initialization and multiboot shim
 */
#include "arch/aarch64/platform.h"
#include "arch/x86/multiboot.h"
#include "kernel/klog.h"
#include "lib/stdint.h"
#include "mm/memory.h"
#include "util/util.h"

extern uint32_t _kernel_end;

extern uint8_t _mod_storage_server_start[], _mod_storage_server_end[];
extern uint8_t _mod_input_server_start[], _mod_input_server_end[];
extern uint8_t _mod_time_server_start[], _mod_time_server_end[];
extern uint8_t _mod_comp_start[], _mod_comp_end[];
extern uint8_t _mod_font_atlas_start[], _mod_font_atlas_end[];
extern uint8_t _mod_icons_start[], _mod_icons_end[];
extern uint8_t _mod_wallpaper_start[], _mod_wallpaper_end[];
extern uint8_t _mod_logo_start[], _mod_logo_end[];
extern uint8_t _mod_terminal_start[], _mod_terminal_end[];
extern uint8_t _mod_ttysh_start[], _mod_ttysh_end[];
extern uint8_t _mod_init_start[], _mod_init_end[];

extern void kmain(uint32_t magic, struct multiboot_info *boot_info);

void arm_map_high_vectors(void);
void arm_install_vectors(void);

#define FB_SIZE (ARM_FB_WIDTH * ARM_FB_HEIGHT * (ARM_FB_BPP / 8))

static struct multiboot_info fake_mboot;
#define MAX_FAKE_MODULES 12
static multiboot_module_t fake_modules[MAX_FAKE_MODULES];
static char mod_cmdlines[MAX_FAKE_MODULES][128];

static int add_module(int idx, uint8_t *start, uint8_t *end, const char *cmdline)
{
    if (start == end) return idx;
    uint32_t phys_start = (uint32_t)start - 0xC0000000 + ARM_PHYS_OFFSET;
    uint32_t phys_end   = (uint32_t)end   - 0xC0000000 + ARM_PHYS_OFFSET;
    fake_modules[idx].mod_start = phys_start;
    fake_modules[idx].mod_end   = phys_end;
    int len = 0;
    while (cmdline[len] && len < 126) { mod_cmdlines[idx][len] = cmdline[len]; len++; }
    mod_cmdlines[idx][len] = 0;
    fake_modules[idx].string = (uint32_t)mod_cmdlines[idx];
    fake_modules[idx].reserved = 0;
    return idx + 1;
}

void platform_init(void)
{
    arm_uart_puts("QuarkOS ARM: platform_init (virt machine)\n");

    arm_map_high_vectors();
    arm_install_vectors();
    arm_uart_puts("  Vectors installed\n");

    arm_gic_init();
    arm_uart_puts("  GIC initialized\n");

    uint32_t kernel_end_phys = (uint32_t)&_kernel_end - 0xC0000000 + ARM_PHYS_OFFSET;
    uint32_t fb_phys = (kernel_end_phys + 0xFFFFF) & ~0xFFFFF;
    uint32_t phys_alloc_start = fb_phys + ((FB_SIZE + 0xFFF) & ~0xFFF);

    memset(&fake_mboot, 0, sizeof(fake_mboot));
    fake_mboot.flags = (1 << 0) | (1 << 6) | (1 << 12);
    fake_mboot.mem_lower = 640;
    fake_mboot.mem_upper = (ARM_RAM_SIZE / 1024) - 1024;

    fake_mboot.framebuffer_addr  = fb_phys;
    fake_mboot.framebuffer_width = ARM_FB_WIDTH;
    fake_mboot.framebuffer_height = ARM_FB_HEIGHT;
    fake_mboot.framebuffer_bpp   = ARM_FB_BPP;
    fake_mboot.framebuffer_pitch = ARM_FB_WIDTH * (ARM_FB_BPP / 8);

    static struct multiboot_mmap_entry mmap[1];
    mmap[0].size     = sizeof(struct multiboot_mmap_entry) - 4;
    mmap[0].addr_low = ARM_PHYS_OFFSET;
    mmap[0].addr_high = 0;
    mmap[0].len_low  = ARM_RAM_SIZE;
    mmap[0].len_high = 0;
    mmap[0].type     = MULTIBOOT_MEMORY_AVAILABLE;
    fake_mboot.mmap_addr   = (uint32_t)mmap;
    fake_mboot.mmap_length = sizeof(mmap);

    int n = 0;
    n = add_module(n, _mod_storage_server_start, _mod_storage_server_end,
                   "storage_server role=service priv=ata");
    n = add_module(n, _mod_input_server_start, _mod_input_server_end,
                   "input_server role=service priv=ps2");
    n = add_module(n, _mod_time_server_start, _mod_time_server_end,
                   "time_server role=service");
    n = add_module(n, _mod_comp_start, _mod_comp_end,
                   "comp role=service");
    n = add_module(n, _mod_font_atlas_start, _mod_font_atlas_end,
                   "type=font");
    n = add_module(n, _mod_icons_start, _mod_icons_end,
                   "type=icons");
    n = add_module(n, _mod_wallpaper_start, _mod_wallpaper_end,
                   "type=wallpaper");
    n = add_module(n, _mod_logo_start, _mod_logo_end,
                   "type=logo");
    n = add_module(n, _mod_ttysh_start, _mod_ttysh_end,
                   "ttysh");
    n = add_module(n, _mod_terminal_start, _mod_terminal_end,
                   "terminal");
    n = add_module(n, _mod_init_start, _mod_init_end,
                   "init");

    fake_mboot.mods_count = n;
    fake_mboot.mods_addr  = (uint32_t)fake_modules;

    phys_alloc_start = (phys_alloc_start + 0xFFF) & ~0xFFF;

    arm_uart_puts("  Initializing virtio-gpu\n");
    virtio_gpu_init(fb_phys);

    arm_uart_puts("  Initializing virtio-input\n");
    virtio_input_init();

    arm_uart_puts("  Initializing virtio-blk\n");
    virtio_blk_init();

    klog_init();
    KLOG_BOOT(LOG_INFO, "QuarkOS 2.0 AArch64 boot");
    KLOG_BOOT(LOG_INFO, "kernel_end_phys=%x fb_phys=%x alloc_start=%x",
              kernel_end_phys, fb_phys, phys_alloc_start);

    arm_uart_puts("  Calling kmain\n");

    kmain(0x2BADB002, &fake_mboot);
}
