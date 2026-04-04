#include "arch/x86/fpu.h"
#include "arch/x86/gdt.h"
#include "arch/x86/idt.h"
#include "arch/x86/multiboot.h"
#include "drivers/GUI/framebuffer.h"
#include "drivers/acpi.h"
#include "drivers/ata.h"
#include "drivers/ps2/keyboard.h"
#include "drivers/ps2/mouse.h"
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
static int ticks_counter = 0;

uint32_t cpu_usage_pct = 0;
static uint32_t busy_ticks = 0;
static uint32_t total_ticks_count = 0;

void k_tick_handler()
{
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
    asm volatile("cli");
    boot_log_add("Interrupts disabled");
    arch_gdt_init();
    boot_log_add("GDT initialized");
    arch_idt_init();
    boot_log_add("IDT initialized");
    uint32_t kernel_end_phys = (uint32_t)&_kernel_end - KERNEL_START;
    uint32_t phys_alloc_start = (kernel_end_phys + 0xFFF) & ~0xFFF;
    if (boot_info->mods_count > 0) {
        multiboot_module_t *mods = (multiboot_module_t *)boot_info->mods_addr;
        for (uint32_t i = 0; i < boot_info->mods_count; i++)
            if (mods[i].mod_end > phys_alloc_start)
                phys_alloc_start = (mods[i].mod_end + 0xFFF) & ~0xFFF;
    }
    memory_init(boot_info, phys_alloc_start);
    boot_log_add_hex("Physical allocator start: ", phys_alloc_start);
    boot_log_add_dec("Physical memory online: ", total_phys_pages / 256, " MB");
    int acpi_ready = acpi_init();
    if (acpi_ready) {
        boot_log_add("ACPI tables parsed");
        boot_log_add_dec("SMP logical processors: ", acpi_get_cpu_count(), "");
        boot_log_add_acpi_capabilities(acpi_power_button_available(), acpi_reset_available());
    } else {
        boot_log_add("WARN: ACPI tables unavailable");
    }
    int power_log = boot_log_add("NOTICE: probing ACPI power interface...");
    boot_log_replace(power_log, acpi_power_ready() ? "NOTICE: ACPI power interface ready"
                                                   : "NOTICE: ACPI power interface unavailable");
    mm_heap_init(0x800000);
    boot_log_add_hex("Kernel heap base: ", 0x00800000);
    drv_fb_init(boot_info);
    boot_log_add_framebuffer_info();
    k_scheduler_init();

    ready_queue->kernel_stack = (uint32_t)&stack_top;
    arch_tss_set_stack((uint32_t)&stack_top);
    boot_log_add("Scheduler initialized");
    int launched = kernel_exec_internal(boot_info);
    boot_log_add_dec("User services launched: ", launched, "");
    drv_rtc_read_datetime((Time *)&sys_time);
    boot_log_add_rtc_line((const Time *)&sys_time);
    draw_boot_splash();
    drv_pit_init();
    boot_log_add("Timer source: PIT (8253) @ 100 Hz");
    drv_stor_ata_init();
    boot_log_add("Storage: ATA PIO userspace driver ready");
    drv_input_keyboard_init();
    boot_log_add("PS/2 keyboard driver ready");
    drv_input_mouse_init();
    boot_log_add("PS/2 mouse driver ready");
    arch_fpu_enable();
    boot_log_add("FPU extensions enabled");
    boot_log_add("Enabling interrupts");
    asm volatile("sti");
    boot_log_add("Boot complete. Entering idle loop");
    for (;;)
        asm volatile("hlt");
}
