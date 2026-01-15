#include "arch/x86/fpu.h"
#include "arch/x86/gdt.h"
#include "arch/x86/idt.h"
#include "arch/x86/multiboot.h"
#include "drivers/GUI/framebuffer.h"
#include "drivers/ps2/keyboard.h"
#include "drivers/ps2/mouse.h"
#include "drivers/rtc.h"
#include "drivers/timer.h"
#include "drivers/ata.h"      
#include "fs/fat.h"           
#include "gfx/bitmaps.h"
#include "gfx/bmp.h"
#include "gfx/events.h"
#include "gfx/font_atlas.h"
#include "lib/math.h"
#include "lib/stdlib.h"
#include "mm/kmalloc.h"
#include "mm/memory.h"
#include "util/util.h"
#include "video/compositor.h"
#include "video/texture.h"
#include "video/vga.h"
#include "video/window.h"
#include "kernel/process.h" 

extern uint32_t _kernel_end;
extern uint32_t stack_top; 

volatile int counter = 0;
volatile int needs_redraw = 0; 
volatile Time sys_time;

static int ticks_counter = 0;
static int sync_rtc_counter = 0;

void timer_callback() {
    Event e;
    while (event_pop(&e)) {
        window_event_handle(e);
        needs_redraw = 1; 
    }

    ticks_counter++;
    if (ticks_counter >= 100) {
        ticks_counter = 0;
        sys_time.second++;
        if (sys_time.second >= 60) {
            sys_time.second = 0;
            sys_time.minute++;
            if (sys_time.minute >= 60) {
                sys_time.minute = 0;
                sys_time.hour++;
                if (sys_time.hour >= 24) {
                    sys_time.hour = 0;
                }
            }
        }
        sync_rtc_counter++;
        if (sync_rtc_counter >= 10) {
            read_rtc((Time *)&sys_time);
            sync_rtc_counter = 0;
        }
        needs_redraw = 1; 
    }
    
    out_port_B(0x20, 0x20); 
    schedule();
}

void kmain(uint32_t magic, struct multiboot_info *boot_info) {
    (void)magic;

    asm volatile("cli");
    gdt_init();
    idt_init();

    uint32_t kernel_end_phys = (uint32_t)&_kernel_end - KERNEL_START;
    uint32_t phys_alloc_start = (kernel_end_phys + 0xFFF) & ~0xFFF;
    
    if (boot_info->mods_count > 0) {
        multiboot_module_t *mods = (multiboot_module_t *)boot_info->mods_addr;
        for (uint32_t i = 0; i < boot_info->mods_count; i++) {
            uint32_t mod_end = mods[i].mod_end;
            mod_end = (mod_end + 0xFFF) & ~0xFFF;
            if (mod_end > phys_alloc_start) phys_alloc_start = mod_end;
        }
    }

    memory_init(boot_info, phys_alloc_start);
    kmalloc_init(0x2000000); 

    framebuffer_init(boot_info);
    
    scheduler_init();
    timer_init();
    fpu_init();
    
    events_init();
    mouse_init();
    keyboard_init();

    window_manager_init();
    compositor_init();
    
    fat_init();

    window_create(100, 100, 300, 200, "Notepad", WIN_TYPE_DEFAULT);
    Window *term = window_create(450, 400, 400, 300, "Kernel Log", WIN_TYPE_TERMINAL);
    window_print(term, "QuarkOS Cyclone 0.3 Kernel\n> ");

    if (boot_info->mods_count > 0) {
        multiboot_module_t *mods = (multiboot_module_t *)boot_info->mods_addr;
        uint32_t font_start = mods[0].mod_start;
        uint32_t font_size = mods[0].mod_end - font_start;
        void *font_ptr = map_module(font_start, font_size);
        font_atlas_init((uint8_t *)font_ptr);
        window_print(term, "Font loaded from Multiboot.\n");
    } else {
        window_print(term, "WARNING: Font module not found!\n");
    }
    
    read_rtc((Time *)&sys_time);
    
    asm volatile("sti");
    
    invalidate_screen();
    compositor_paint();

    swap_buffers();

    for (;;) {
        asm volatile("cli");
        if (needs_redraw) {
            needs_redraw = 0;
            compositor_paint();
            asm volatile("sti");
        } else {
            asm volatile("sti");
            asm volatile("hlt");
        }
    }
}