#include "arch/aarch64/platform.h"
#include "kernel/core/bootinfo.h"
#include "kernel/core/klog.h"
#include "drivers/rtc/rtc.h"
#include "stdint.h"
#include "kernel/mm/memory.h"
#include "kernel/lib/util.h"

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

extern void kmain(boot_info_t *boot_info);

void arm_install_vectors(void);

#define FB_SIZE (ARM_FB_WIDTH * ARM_FB_HEIGHT * (ARM_FB_BPP / 8))

static boot_info_t boot_info;
#define MAX_BOOT_MODULES 12
static boot_module_t boot_modules[MAX_BOOT_MODULES];
static char module_command_lines[MAX_BOOT_MODULES][128];

static int add_module(int idx, uint8_t *start, uint8_t *end, const char *cmdline) {
    if (start == end)
        return idx;
    uint32_t phys_start = (uint32_t)(uintptr_t)start - 0xC0000000 + ARM_PHYS_OFFSET;
    uint32_t phys_end = (uint32_t)(uintptr_t)end - 0xC0000000 + ARM_PHYS_OFFSET;
    boot_modules[idx].start = phys_start;
    boot_modules[idx].end = phys_end;
    int len = 0;
    while (cmdline[len] && len < 126) {
        module_command_lines[idx][len] = cmdline[len];
        len++;
    }
    module_command_lines[idx][len] = 0;
    boot_modules[idx].command_line = (uint32_t)(uintptr_t)module_command_lines[idx];
    return idx + 1;
}

void platform_init(void) {
    arm_uart_puts("Sodium ARM: platform_init (virt machine)\n");

    arm_install_vectors();
    arm_uart_puts("  Vectors installed\n");

    arm_gic_init();
    arm_uart_puts("  GIC initialized\n");

    uint32_t kernel_end_phys = (uint32_t)(uintptr_t)&_kernel_end - 0xC0000000 + ARM_PHYS_OFFSET;
    uint32_t fb_phys = (kernel_end_phys + 0xFFFFF) & ~0xFFFFF;
    uint32_t phys_alloc_start = fb_phys + ((FB_SIZE + 0xFFF) & ~0xFFF);

    memset(&boot_info, 0, sizeof(boot_info));
    boot_info.ram_size = ARM_RAM_SIZE;
    boot_info.framebuffer_addr = fb_phys;
    boot_info.framebuffer_width = ARM_FB_WIDTH;
    boot_info.framebuffer_height = ARM_FB_HEIGHT;
    boot_info.framebuffer_bpp = ARM_FB_BPP;
    boot_info.framebuffer_pitch = ARM_FB_WIDTH * (ARM_FB_BPP / 8);

    int n = 0;
    n = add_module(n, _mod_storage_server_start, _mod_storage_server_end,
                   "storage_server role=service");
    n = add_module(n, _mod_input_server_start, _mod_input_server_end,
                   "input_server role=service");
    n = add_module(n, _mod_time_server_start, _mod_time_server_end, "time_server role=service");
    n = add_module(n, _mod_comp_start, _mod_comp_end, "comp role=service");
    n = add_module(n, _mod_font_atlas_start, _mod_font_atlas_end, "type=font");
    n = add_module(n, _mod_icons_start, _mod_icons_end, "type=icons");
    n = add_module(n, _mod_wallpaper_start, _mod_wallpaper_end, "type=wallpaper");
    n = add_module(n, _mod_logo_start, _mod_logo_end, "type=logo");
    n = add_module(n, _mod_ttysh_start, _mod_ttysh_end, "ttysh");
    n = add_module(n, _mod_terminal_start, _mod_terminal_end, "terminal");
    n = add_module(n, _mod_init_start, _mod_init_end, "init");

    boot_info.module_count = n;
    boot_info.modules_addr = (uint32_t)(uintptr_t)boot_modules;

    phys_alloc_start = (phys_alloc_start + 0xFFF) & ~0xFFF;

    arm_uart_puts("  Initializing virtio-gpu\n");
    virtio_gpu_init(fb_phys);

    arm_uart_puts("  Initializing virtio-input\n");
    virtio_input_init();

    arm_uart_puts("  Initializing virtio-blk\n");
    virtio_blk_init();

    klog_init();
    KLOG_BOOT(LOG_INFO, "Sodium AArch64 boot");
    KLOG_BOOT(LOG_INFO, "kernel_end_phys=%x fb_phys=%x alloc_start=%x", kernel_end_phys, fb_phys,
              phys_alloc_start);

    arm_uart_puts("  Calling kmain\n");

    kmain(&boot_info);
}

void arm_psci_system_off(void) {
    register uint64_t function_id asm("x0") = 0x84000008;
    asm volatile("hvc #0" : "+r"(function_id) : : "memory");
    for (;;)
        arm_wfi();
}

void arm_psci_system_reset(void) {
    register uint64_t function_id asm("x0") = 0x84000009;
    asm volatile("hvc #0" : "+r"(function_id) : : "memory");
    for (;;)
        arm_wfi();
}
