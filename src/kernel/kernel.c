// SPDX-License-Identifier: MIT
/*
 * src/kernel/kernel.c - Kernel entry point, hardware initialization, and idle loop
 */
#include "arch/aarch64/platform.h"
#include "kernel/klog.h"

void arch_gdt_init(void);
void arch_idt_init(void);
void arch_tss_set_stack(uint32_t kernel_stack);
void arch_fpu_enable(void);
int acpi_init(void);
int acpi_get_cpu_count(void);
int acpi_power_button_available(void);
int acpi_reset_available(void);
int acpi_power_ready(void);
int drv_sb16_init(void);

void drv_input_keyboard_init(void);
void drv_input_mouse_init(void);
#include "arch/x86/multiboot.h"
#include "drivers/GUI/framebuffer.h"
#include "drivers/ata.h"
#include "fs/fat.h"
#include "gfx/font_atlas.h"
#include "drivers/rtc.h"
#include "drivers/timer.h"
#include "kernel/bootsplash.h"
#include "kernel/ipc.h"
#include "kernel/process.h"
#include "lib/stdlib.h"
#include "mm/kmalloc.h"
#include "mm/memory.h"
extern uint32_t _kernel_end;
extern uint32_t stack_top;
extern process_t *ready_queue;
extern int sys_set_server_pid(int type, int pid);
extern int sys_grant_io_privilege(int pid, uint32_t mask);
extern void k_scheduler_update_sleeps();
extern uint32_t total_phys_pages;
uint32_t k_font_size = 0;
uint32_t k_font_phys_addr = 0;
void *k_font_virt_addr = 0;
uint32_t k_icons_phys_start = 0;
uint32_t k_icons_size = 0;
uint32_t k_wallpaper_phys_addr = 0;
uint32_t k_wallpaper_size = 0;
uint32_t k_logo_phys_addr = 0;
uint32_t k_logo_size = 0;
volatile Time sys_time;
static volatile int ticks_counter = 0;
uint32_t cpu_usage_pct = 0;
static volatile uint32_t busy_ticks = 0;
static volatile uint32_t total_ticks_count = 0;
extern void validate_current(const char *tag);
void k_tick_handler()
{
    validate_current("tick");
    total_ticks_count++;
    if (current_process && current_process->pid != 0)
        busy_ticks++;
    ticks_counter++;
    k_scheduler_update_sleeps();

    {
        static uint8_t was_zombie[16];
        static volatile int diag_counter = 0;
        process_t *p = ready_queue;
        if (p) {
            int count = 0;
            do {

                if (p->pid > 0 && p->pid < 16 && (int)p->state == 3 && !was_zombie[p->pid]) {
                    was_zombie[p->pid] = 1;
                    KLOG_PROC(LOG_CRIT, "ZOMBIE pid=%d name='%s' in ready_queue!",
                              p->pid, p->name);
                }
                p = p->next;
                count++;
            } while (p != ready_queue && count < 16);
        }

        diag_counter++;
        if (diag_counter >= 500) {
            diag_counter = 0;
            const char state_ch[] = "ROBWIZP";
            p = ready_queue;
            if (p) {
                int n = 0;
                do {
                    int st = (int)p->state;
                    char sc = (st >= 0 && st <= 6) ? state_ch[st] : '?';
                    KLOG_SCHED(LOG_INFO, "ps %d:%c %s q=%d",
                               p->pid, sc,
                               p->name[0] ? p->name : "(anon)",
                               p->ipc_queue.count);
                    p = p->next;
                    n++;
                } while (p != ready_queue && n < 16);
            }
        }
    }

    if (ticks_counter >= 100) {
        cpu_usage_pct = (busy_ticks * 100) / total_ticks_count;
        busy_ticks = 0;
        total_ticks_count = 0;
        ticks_counter = 0;
        sys_time.second++;
        if (sys_time.second >= 60) {
            sys_time.second = 0;
            sys_time.minute++;
            if (sys_time.minute >= 60) {
                sys_time.minute = 0;
                sys_time.hour++;
                if (sys_time.hour >= 24)
                    sys_time.hour = 0;
            }
        }
    }
    k_scheduler_yield();
}
static int kernel_exec_internal(struct multiboot_info *boot_info)
{
    int launched = 0;
    if (boot_info->mods_count > 0) {
        multiboot_module_t *mods = (multiboot_module_t *)boot_info->mods_addr;
        int pid_tracker = 1;
        for (uint32_t i = 0; i < boot_info->mods_count; i++) {
            uint32_t size = mods[i].mod_end - mods[i].mod_start;
            void *virt_addr = map_module(mods[i].mod_start, size);
            char *cmdline = (char *)mods[i].string;
            if (cmdline && strstr(cmdline, "type=wallpaper")) {
                k_wallpaper_phys_addr = mods[i].mod_start;
                k_wallpaper_size = size;
                continue;
            }
            if (cmdline && strstr(cmdline, "type=icons")) {
                k_icons_phys_start = mods[i].mod_start;
                k_icons_size = size;
                continue;
            }
            if (cmdline && strstr(cmdline, "type=logo")) {
                k_logo_phys_addr = mods[i].mod_start;
                k_logo_size = size;
                continue;
            }
            if (virt_addr && *(uint32_t *)virt_addr == 0x464F4E54) {
                k_font_phys_addr = mods[i].mod_start;
                k_font_size = size;
                k_font_virt_addr = virt_addr;
                continue;
            }
            if (virt_addr && *(uint32_t *)virt_addr == 0x5845517F) {
                k_proc_spawn((uint8_t *)virt_addr, size);
                process_t *new_p = ready_queue->next;
                if (cmdline && new_p) {
                    char *s = cmdline;
                    while (*s) {
                        while (*s == ' ' || *s == '"')
                            s++;
                        if (!*s)
                            break;
                        char *token_start = s;
                        int has_equal = 0;
                        while (*s && *s != ' ' && *s != '"') {
                            if (*s == '=')
                                has_equal = 1;
                            s++;
                        }
                        if (!has_equal) {
                            char *name_start = token_start;
                            for (char *p = token_start; p < s; p++)
                                if (*p == '/')
                                    name_start = p + 1;
                            int len = 0;
                            while (name_start + len < s && name_start[len] != '.' && len < 15) {
                                new_p->name[len] = name_start[len];
                                len++;
                            }
                            new_p->name[len] = 0;
                            break;
                        }
                    }
                }
                int current_pid = pid_tracker++;
                if (cmdline && strstr(cmdline, "priv=ata")) {
                    sys_set_server_pid(0, current_pid);
                    sys_grant_io_privilege(current_pid, 0x01);
                }
                if (cmdline && strstr(cmdline, "priv=ps2"))
                    sys_grant_io_privilege(current_pid, 0x02);
                if (cmdline && (strstr(cmdline, "comp") || strstr(cmdline, "comp.qex")))
                    sys_set_server_pid(1, current_pid);
                launched++;
            }
        }
    }
    return launched;
}
void kmain(uint32_t magic, struct multiboot_info *boot_info)
{
    (void)magic;
    boot_log_record_boot_info(boot_info);
    arm_irq_disable();
    boot_log_add("Interrupts disabled");
    arch_gdt_init();
    boot_log_add("GDT initialized");
    arch_idt_init();
    boot_log_add("IDT initialized");
    uint32_t kernel_end_phys = (uint32_t)(uintptr_t)&_kernel_end - KERNEL_START + 0x40000000;
    uint32_t phys_alloc_start = (kernel_end_phys + 0xFFF) & ~0xFFF;
    if (boot_info->mods_count > 0) {
        multiboot_module_t *mods = (multiboot_module_t *)boot_info->mods_addr;
        for (uint32_t i = 0; i < boot_info->mods_count; i++)
            if (mods[i].mod_end > phys_alloc_start)
                phys_alloc_start = (mods[i].mod_end + 0xFFF) & ~0xFFF;
    }
    {
        uint32_t fb_phys = boot_info->framebuffer_addr;
        uint32_t fb_bytes = boot_info->framebuffer_width *
                            boot_info->framebuffer_height *
                            (boot_info->framebuffer_bpp / 8);
        uint32_t fb_end = (fb_phys + fb_bytes + 0xFFF) & ~0xFFF;
        if (fb_end > phys_alloc_start)
            phys_alloc_start = fb_end;
    }
    memory_init(boot_info, phys_alloc_start);
    boot_log_add_hex("Physical allocator start: ", phys_alloc_start);
    boot_log_add_dec("Physical memory online: ", total_phys_pages / 256, " MB");
    int acpi_ready = acpi_init();
    if (acpi_ready) {
        boot_log_add("Platform: QEMU virt (PSCI power control)");
        boot_log_add_dec("SMP logical processors: ", acpi_get_cpu_count(), "");
        boot_log_add_acpi_capabilities(acpi_power_button_available(), acpi_reset_available());
    } else {
        boot_log_add("WARN: ACPI tables unavailable");
    }
    int power_log = boot_log_add("NOTICE: probing power interface...");
    boot_log_replace(power_log, acpi_power_ready() ? "NOTICE: power interface ready (PSCI)"
                                                   : "NOTICE: power interface unavailable");
    mm_heap_init(0x800000);
    boot_log_add_hex("Kernel heap base: ", 0x00800000);
    drv_fb_init(boot_info);
    boot_log_add_framebuffer_info();
    k_scheduler_init();
    ready_queue->kernel_stack = (uintptr_t)&stack_top;
    arch_tss_set_stack((uintptr_t)&stack_top);
    boot_log_add("Scheduler initialized");
    boot_log_add("Storage: virtio-blk ready");
    fat_init();
    boot_log_add("FAT16: filesystem ready");
    int launched = kernel_exec_internal(boot_info);
    boot_log_add_dec("User services launched: ", launched, "");
    if (!k_font_virt_addr) {
        k_font_virt_addr = font_atlas_load_from_ttf(&k_font_size);
        if (k_font_virt_addr)
            boot_log_add("Font: loaded from disk");
        else
            boot_log_add("Font: WARN failed to load from disk");
    }
    drv_rtc_read_datetime((Time *)&sys_time);
    boot_log_add_rtc_line((const Time *)&sys_time);
    draw_boot_splash();
#define DBG_SQ_SIZE 60
#define DBG_SQ_STEP 64

#define DBG_SQUARE(col, color, label) \
    do { \
        arm_uart_puts("DBG: " label "\n"); \
        if (fb_ptr && fb_width && fb_height) { \
            uint32_t _x0 = fb_width > (uint32_t)((col)*DBG_SQ_STEP + DBG_SQ_SIZE) \
                           ? fb_width - (uint32_t)((col)*DBG_SQ_STEP + DBG_SQ_SIZE) : 0; \
            for (int _y = 0; _y < DBG_SQ_SIZE; _y++) \
                for (int _x = 0; _x < DBG_SQ_SIZE; _x++) \
                    ((uint32_t *)((uint8_t *)fb_ptr + \
                        ((uint32_t)_y * fb_pitch) + \
                        ((_x0 + (uint32_t)_x) * 4)))[0] = (color); \
            virtio_gpu_flush(); \
        } \
    } while (0)
    extern void virtio_gpu_flush(void);

    DBG_SQUARE(0, 0x00FF0000, "0 bootsplash done");

    extern void arm_timer_init(void);
    arm_timer_init();
    drv_pit_init();
    boot_log_add("Timer source: Private Timer @ 100 Hz");

    DBG_SQUARE(1, 0x0000FF00, "1 timer init done");

    extern void virtio_input_init(void);
    virtio_input_init();
    boot_log_add("Virtio input driver ready");
    boot_log_add("Virtio mouse driver ready");

    DBG_SQUARE(2, 0x000000FF, "2 virtio_input done");

    arch_fpu_enable();
    boot_log_add("FPU extensions enabled");
    if (drv_sb16_init() == 0)
        boot_log_add("Audio: SB16 driver ready");
    else
        boot_log_add("Audio: SB16 not found (no sound card)");

    DBG_SQUARE(3, 0x00FFFF00, "3 FPU+audio done");

    boot_log_add("Enabling interrupts");
    arm_uart_puts("  About to enable IRQs\n");
    arm_irq_enable();

    DBG_SQUARE(4, 0x0000FFFF, "4 IRQs enabled");
    arm_uart_puts("  IRQs enabled, entering idle loop\n");
    boot_log_add("Boot complete. Entering idle loop");
    {

        DBG_SQUARE(5, 0x00FFFFFF, "5 entering idle loop");
        {
            static const uint32_t hb_colors[] = {
                0x00FF0000, 0x0000FF00, 0x000000FF,
                0x00FFFF00, 0x00FF00FF, 0x0000FFFF,
            };
            int hb_idx = 0;
            int hb_tick = 0;

            for (;;) {
                arm_irq_enable();
                arm_wfi();
            }
        }
    }
}
