#include "arch/x86/fpu.h"
#include "arch/x86/gdt.h"
#include "arch/x86/idt.h"
#include "arch/x86/multiboot.h"
#include "drivers/GUI/framebuffer.h"
#include "drivers/acpi.h"
#include "drivers/ps2/keyboard.h"
#include "drivers/ps2/mouse.h"
#include "drivers/rtc.h"
#include "drivers/timer.h"
#include "drivers/ata.h"
#include "kernel/ipc.h"
#include "kernel/process.h"
#include "mm/memory.h"
#include "mm/kmalloc.h"
#include "lib/stdlib.h" 
#include "gfx/bmp.h"

typedef struct {
    uint16_t x, y, w, h;
    int16_t advance, off_x, off_y;
} __attribute__((packed)) K_GlyphInfo;

typedef struct {
    uint32_t magic;
    uint32_t tex_width, tex_height, line_height, ascent, count;
    K_GlyphInfo *glyphs;
    uint32_t *bitmap;
} K_AtlasFont;

extern uint32_t _kernel_end;
extern uint32_t stack_top;
extern process_t *ready_queue;
extern int sys_set_server_pid(int type, int pid);
extern int sys_grant_io_privilege(int pid, uint32_t mask);
extern void k_scheduler_update_sleeps();
extern void draw_bmp(void* bmp_data, int x, int y);

uint32_t k_font_size = 0;
uint32_t k_font_phys_addr = 0;
void* k_font_virt_addr = 0;
uint32_t k_icons_phys_start = 0;
uint32_t k_icons_size = 0;
uint32_t k_wallpaper_phys_addr = 0;
uint32_t k_wallpaper_size = 0;
uint32_t k_logo_phys_addr = 0;
uint32_t k_logo_size = 0;

K_AtlasFont k_boot_font;
volatile Time sys_time;
static int ticks_counter = 0;

uint32_t cpu_usage_pct = 0;
static uint32_t busy_ticks = 0;
static uint32_t total_ticks_count = 0;

static const char *const BOOT_POWER_PLACEHOLDER = "[    0.000812] NOTICE: probing ACPI power interface...";

static const char* boot_log[] = {
    "[    0.000000] Blanket core 0.3.1 (x86_32) booting...",
    "[    0.000000] Command line: quiet splash",
    "[    0.000000] Detected CPU: Intel Core (family 6, model 158)",
    "[    0.000000] SMP: 4 logical processors",
    "[    0.000000] Initializing memory manager",
    "[    0.000012] Memory: 4096 MB total",
    "[    0.000017] Memory: 3898 MB available",
    "[    0.000045] Initializing page tables",
    "[    0.000091] IDT initialized",
    "[    0.000110] PIC: legacy interrupt controller",
    "[    0.000132] Timer: HPET available",
    "[    0.000158] Calibrating delay loop... 3187.24 BogoMIPS",
    "[    0.000189] PCI: scanning bus",
    "[    0.000245] PCI: 00:02.0 VGA controller detected",
    "[    0.000291] PCI: 00:1f.2 SATA controller detected",
    "[    0.000328] Loading driver: vga",
    "[    0.000356] Loading driver: ps2kbd",
    "[    0.000379] Loading driver: ps2mouse",
    "[    0.000411] Framebuffer initialized (1920x1080)",
    "[    0.000438] Console: framebuffer output enabled",
    "[    0.000476] Scheduler initialized",
    "[    0.000509] Starting system services",
    "[    0.000532] service devmgr: started",
    "[    0.000556] service syslog: started",
    "[    0.000579] service input: started",
    "[    0.000603] service display: started",
    "[    0.000645] Mounting root filesystem (NChFS)",
    "[    0.000682] INFO: filesystem journal replay complete",
    "[    0.000712] rootfs mounted",
    "[    0.000736] login manager ready",
    "[    0.000789] NOTICE: display driver running in compatibility mode",
    BOOT_POWER_PLACEHOLDER,
    "[    0.000835] QuarkOS initialization complete",
    "[    0.000858] Welcome to QuarkOS"
};

static void boot_log_update_power(const char *status) {
    uint32_t count = sizeof(boot_log) / sizeof(boot_log[0]);
    for (uint32_t i = 0; i < count; i++) {
        if (boot_log[i] == BOOT_POWER_PLACEHOLDER) {
            boot_log[i] = status;
            return;
        }
    }
}

void k_font_init(void* data) {
    if (!data) return;
    uint32_t* ptr = (uint32_t*)data;
    k_boot_font.magic = ptr[0];
    k_boot_font.tex_width = ptr[1];
    k_boot_font.tex_height = ptr[2];
    k_boot_font.line_height = ptr[3];
    k_boot_font.ascent = ptr[4];
    k_boot_font.count = ptr[5];
    k_boot_font.glyphs = (K_GlyphInfo*)(ptr + 6);
    k_boot_font.bitmap = (uint32_t*)((uint8_t*)k_boot_font.glyphs + k_boot_font.count * sizeof(K_GlyphInfo));
}

int k_draw_string_atlas(int x, int y, const char* str, uint32_t color) {
    if (!k_font_virt_addr) return x;
    while (*str) {
        char c = *str++;
        if (c < 32 || c > 126) continue;
        K_GlyphInfo* g = &k_boot_font.glyphs[c - 32];
        for (int iy = 0; iy < (int)g->h; iy++) {
            for (int ix = 0; ix < (int)g->w; ix++) {
                uint32_t px = k_boot_font.bitmap[(g->y + iy) * k_boot_font.tex_width + (g->x + ix)];
                uint8_t a = (px >> 24) & 0xFF;
                if (a > 0) put_pixel_AA(x + g->off_x + ix, y + g->off_y + iy, color, a);
            }
        }
        x += g->advance;
    }
    return x;
}

void k_draw_boot_line(int x, int y, const char* str) {
    const char* bracket_end = str;
    while (*bracket_end && *bracket_end != ']') bracket_end++;
    uint32_t text_color = 0x888888;
    if (strstr(str, "NOTICE")) text_color = COLOR_WHITE;
    else if (strstr(str, "INFO")) text_color = 0x44CC44;
    if (*bracket_end == ']') {
        char prefix[32];
        int len = (bracket_end - str) + 1;
        if (len > 31) len = 31;
        strncpy(prefix, str, len);
        prefix[len] = 0;
        int next_x = k_draw_string_atlas(x, y, prefix, 0x555555);
        k_draw_string_atlas(next_x, y, bracket_end + 1, text_color);
    } else {
        k_draw_string_atlas(x, y, str, text_color);
    }
}

void draw_boot_splash() {
    if (k_logo_phys_addr == 0 || !k_font_virt_addr) return;
    void* logo_data = map_module(k_logo_phys_addr, k_logo_size);
    k_font_init(k_font_virt_addr);
    BITMAPINFOHEADER* info = (BITMAPINFOHEADER*)(logo_data + sizeof(BITMAPFILEHEADER));
    int bar_w = 200, bar_h = 2, gap = 70;
    int scaled_w = info->bi_width, scaled_h = info->bi_height;
    int logo_x = (fb_width - scaled_w) / 2, logo_y = (fb_height - (scaled_h + gap + bar_h)) / 2;
    int bar_x = (fb_width - bar_w) / 2, bar_y = logo_y + scaled_h + gap;
    int last_line = -1;
    for (int i = 0; i <= 100; i++) {
        clear_screen(COLOR_BLACK); 
        draw_bmp(logo_data, logo_x, logo_y);
        rect_fill(bar_x, bar_y, bar_w, bar_h, 0x1A1A1A);
        rect_fill(bar_x, bar_y, (bar_w * i) / 100, bar_h, COLOR_WHITE);
        int current_line_limit = (i * 34) / 100;
        for (int m = 0; m <= current_line_limit; m++) {
            k_draw_boot_line(15, 20 + (m * (k_boot_font.line_height - 2)), boot_log[m]);
        }
        drv_fb_swap_buffers();
        int delay_multiplier = 1;
        if (current_line_limit != last_line) {
            if (current_line_limit == 4 || current_line_limit == 12 || 
                current_line_limit == 18 || current_line_limit == 26 || 
                current_line_limit == 30) delay_multiplier = 15;
            last_line = current_line_limit;
        }
        for (volatile int d = 0; d < 800000 * delay_multiplier; d++) __asm__("pause");
    }
}

void k_tick_handler() {
    total_ticks_count++;
    if (current_process && current_process->pid != 0) busy_ticks++;
    ticks_counter++;
    k_scheduler_update_sleeps();
    if (ticks_counter >= 100) {
        cpu_usage_pct = (busy_ticks * 100) / total_ticks_count;
        busy_ticks = 0; total_ticks_count = 0;
        ticks_counter = 0;
        sys_time.second++;
        if (sys_time.second >= 60) {
            sys_time.second = 0; sys_time.minute++;
            if (sys_time.minute >= 60) {
                sys_time.minute = 0; sys_time.hour++;
                if (sys_time.hour >= 24) sys_time.hour = 0;
            }
        }
    }
    k_scheduler_yield();
}

void kernel_exec_internal(struct multiboot_info *boot_info) {
    if (boot_info->mods_count > 0) {
        multiboot_module_t *mods = (multiboot_module_t *)boot_info->mods_addr;
        int pid_tracker = 1;
        for (uint32_t i = 0; i < boot_info->mods_count; i++) {
            uint32_t size = mods[i].mod_end - mods[i].mod_start;
            void *virt_addr = map_module(mods[i].mod_start, size);
            char* cmdline = (char*)mods[i].string;
            if (cmdline && strstr(cmdline, "type=wallpaper")) {
                k_wallpaper_phys_addr = mods[i].mod_start; k_wallpaper_size = size;
                continue;
            }
            if (cmdline && strstr(cmdline, "type=icons")) {
                k_icons_phys_start = mods[i].mod_start; k_icons_size = size;
                continue;
            }
            if (cmdline && strstr(cmdline, "type=logo")) {
                k_logo_phys_addr = mods[i].mod_start; k_logo_size = size;
                continue;
            }
            if (virt_addr && *(uint32_t*)virt_addr == 0x464F4E54) {
                k_font_phys_addr = mods[i].mod_start; k_font_size = size; k_font_virt_addr = virt_addr;
                continue;
            }
            if (virt_addr && *(uint32_t*)virt_addr == 0x5845517F) {
                k_proc_spawn((uint8_t*)virt_addr, size);
                process_t* new_p = ready_queue->next;
                if (cmdline && new_p) {
                    char *s = cmdline;
                    while (*s) {
                        while (*s == ' ' || *s == '"') s++;
                        if (!*s) break;
                        char *token_start = s;
                        int has_equal = 0;
                        while (*s && *s != ' ' && *s != '"') {
                            if (*s == '=') has_equal = 1;
                            s++;
                        }
                        if (!has_equal) {
                            char *name_start = token_start;
                            for (char *p = token_start; p < s; p++) if (*p == '/') name_start = p + 1;
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
                if (cmdline && strstr(cmdline, "priv=ps2")) sys_grant_io_privilege(current_pid, 0x02);
                if (cmdline && (strstr(cmdline, "comp") || strstr(cmdline, "comp.qex"))) sys_set_server_pid(1, current_pid);
            }
        }
    }
}

void kmain(uint32_t magic, struct multiboot_info *boot_info) {
    (void)magic;
    asm volatile("cli");
    arch_gdt_init();
    arch_idt_init();
    uint32_t kernel_end_phys = (uint32_t)&_kernel_end - KERNEL_START;
    uint32_t phys_alloc_start = (kernel_end_phys + 0xFFF) & ~0xFFF;
    if (boot_info->mods_count > 0) {
        multiboot_module_t *mods = (multiboot_module_t *)boot_info->mods_addr;
        for (uint32_t i = 0; i < boot_info->mods_count; i++) if (mods[i].mod_end > phys_alloc_start) phys_alloc_start = (mods[i].mod_end + 0xFFF) & ~0xFFF;
    }
    memory_init(boot_info, phys_alloc_start);
    acpi_init();
    mm_heap_init(0x800000);
    drv_fb_init(boot_info);
    k_scheduler_init();
    kernel_exec_internal(boot_info);
    drv_rtc_read_datetime((Time*)&sys_time);
    draw_boot_splash();
    drv_pit_init();
    drv_stor_ata_init();
    drv_input_keyboard_init();
    drv_input_mouse_init();
    arch_fpu_enable();
    asm volatile("sti");
    for (;;) asm volatile("hlt");
}