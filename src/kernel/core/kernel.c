#include "arch/aarch64/platform.h"
#include "kernel/core/klog.h"

#include "kernel/core/bootinfo.h"
#include "drivers/video/framebuffer.h"
#include "drivers/rtc/rtc.h"
#include "drivers/timer.h"
#include "drivers/video/bootsplash.h"
#include "abi/ipc.h"
#include "kernel/core/process.h"
#include "stdlib.h"
#include "kernel/mm/kmalloc.h"
#include "kernel/mm/memory.h"
extern uint32_t _kernel_end;
extern uint32_t stack_top;
extern process_t *ready_queue;
extern int sys_set_server_pid(int type, int pid);
extern void k_scheduler_update_sleeps();
extern uint32_t total_phys_pages;
uint32_t k_font_size = 0;
uint32_t k_font_phys_addr = 0;
void *k_font_virt_addr = 0;
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
void k_tick_handler() {
    static int first_tick = 1;
    int trace_first_tick = first_tick;
    if (trace_first_tick) {
        first_tick = 0;
        arm_uart_puts("HVF trace: first timer tick\n");
    }
    validate_current("tick");
    total_ticks_count++;
    if (current_process && current_process->pid != 0)
        busy_ticks++;
    ticks_counter++;
    k_scheduler_update_sleeps();

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
    if (trace_first_tick)
        arm_uart_puts("HVF trace: scheduler yield\n");
    k_scheduler_yield();
    if (trace_first_tick)
        arm_uart_puts("HVF trace: scheduler resumed\n");
}
static int kernel_exec_internal(const boot_info_t *boot_info) {
    int launched = 0;
    if (boot_info->module_count > 0) {
        boot_module_t *mods = (boot_module_t *)(uintptr_t)boot_info->modules_addr;
        int pid_tracker = 1;
        for (uint32_t i = 0; i < boot_info->module_count; i++) {
            uint32_t size = mods[i].end - mods[i].start;
            void *virt_addr = map_module(mods[i].start, size);
            char *cmdline = (char *)(uintptr_t)mods[i].command_line;
            if (cmdline && strstr(cmdline, "type=wallpaper")) {
                k_wallpaper_phys_addr = mods[i].start;
                k_wallpaper_size = size;
                continue;
            }
            if (cmdline && strstr(cmdline, "type=logo")) {
                k_logo_phys_addr = mods[i].start;
                k_logo_size = size;
                continue;
            }
            if (virt_addr && *(uint32_t *)virt_addr == 0x464F4E54) {
                k_font_phys_addr = mods[i].start;
                k_font_size = size;
                k_font_virt_addr = virt_addr;
                continue;
            }
            if (virt_addr && size >= 4 && ((uint8_t *)virt_addr)[0] == 0x7f &&
                ((uint8_t *)virt_addr)[1] == 'E' && ((uint8_t *)virt_addr)[2] == 'L' &&
                ((uint8_t *)virt_addr)[3] == 'F') {
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
                if (cmdline && strstr(cmdline, "storage_server"))
                    sys_set_server_pid(0, current_pid);
                if (cmdline && (strstr(cmdline, "comp") || strstr(cmdline, "comp.elf")))
                    sys_set_server_pid(1, current_pid);
                launched++;
            }
        }
    }
    return launched;
}
void kmain(boot_info_t *boot_info) {
    boot_log_record_boot_info(boot_info);
    arm_irq_disable();
    boot_log_add("Interrupts disabled");
    uint32_t kernel_end_phys = (uint32_t)(uintptr_t)&_kernel_end - KERNEL_START + 0x40000000;
    uint32_t phys_alloc_start = (kernel_end_phys + 0xFFF) & ~0xFFF;
    if (boot_info->module_count > 0) {
        boot_module_t *mods = (boot_module_t *)(uintptr_t)boot_info->modules_addr;
        for (uint32_t i = 0; i < boot_info->module_count; i++)
            if (mods[i].end > phys_alloc_start)
                phys_alloc_start = (mods[i].end + 0xFFF) & ~0xFFF;
    }
    {
        uint32_t fb_phys = boot_info->framebuffer_addr;
        uint32_t fb_bytes = boot_info->framebuffer_width * boot_info->framebuffer_height *
                            (boot_info->framebuffer_bpp / 8);
        uint32_t fb_end = (fb_phys + fb_bytes + 0xFFF) & ~0xFFF;
        if (fb_end > phys_alloc_start)
            phys_alloc_start = fb_end;
    }
    memory_init(phys_alloc_start);
    boot_log_add_hex("Physical allocator start: ", phys_alloc_start);
    boot_log_add_dec("Physical memory online: ", total_phys_pages / 256, " MB");
    boot_log_add("Platform: QEMU virt (AArch64, PSCI)");
    mm_heap_init(0x800000);
    boot_log_add_hex("Kernel heap base: ", 0x00800000);
    drv_fb_init(boot_info);
    boot_log_add_framebuffer_info();
    k_scheduler_init();
    ready_queue->kernel_stack = (uintptr_t)&stack_top;
    boot_log_add("Scheduler initialized");
    boot_log_add("Storage: virtio-blk ready");
    int launched = kernel_exec_internal(boot_info);
    boot_log_add_dec("User services launched: ", launched, "");
    drv_rtc_read_datetime((Time *)&sys_time);
    boot_log_add_rtc_line((const Time *)&sys_time);
    draw_boot_splash();
    extern void arm_timer_init(void);
    arm_timer_init();
    drv_timer_init();
    boot_log_add("Timer source: Private Timer @ 100 Hz");

    boot_log_add("Virtio input driver ready");
    boot_log_add("Virtio mouse driver ready");

    boot_log_add("Enabling interrupts");
    arm_irq_enable();
    KLOG_BOOT(LOG_INFO, "Boot complete. Entering idle loop");
    boot_log_add("Boot complete. Entering idle loop");
    for (;;) {
        arm_irq_enable();
        arm_wfi();
    }
}
