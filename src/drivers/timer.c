#include "drivers/timer.h"
#include "arch/x86/idt.h"
#include "lib/stdint.h"
#include "util/util.h"
#include "video/vga.h"

volatile uint64_t ticks = 0;
const uint32_t freq = 100;

#define MAX_TIMERS 16
static Timer software_timers[MAX_TIMERS];
static int next_timer_id = 1;

extern void timer_callback();

void on_irq_0(struct InterruptRegisters *regs) {
    (void)regs;
    ticks++;
    timer_callback();

    for (int i = 0; i < MAX_TIMERS; i++) {
        if (software_timers[i].id != 0) {
            if (ticks >= software_timers[i].expires_at) {
                software_timers[i].callback();

                if (software_timers[i].interval > 0) {
                    software_timers[i].expires_at = ticks + software_timers[i].interval;
                } else {
                    software_timers[i].id = 0;
                }
            }
        }
    }
}

void timer_init() {
    ticks = 0;

    for (int i = 0; i < MAX_TIMERS; i++)
        software_timers[i].id = 0;

    irq_install_handler(0, &on_irq_0);

    uint32_t divisor = 1193180 / freq;
    out_port_B(0x43, 0x36);
    out_port_B(0x40, (uint8_t)(divisor & 0xFF));
    out_port_B(0x40, (uint8_t)(divisor >> 8) & 0xFF);
}

uint64_t get_uptime_ms() {
    return ticks * (1000 / freq);
}

void sleep(uint32_t ms) {
    uint64_t end_time = ticks + (ms / (1000 / freq));
    while (ticks < end_time) {
        asm volatile("sti");
        asm volatile("hlt");
    }
}

int add_timer(uint32_t ms, TimerCallback callback, int is_interval) {
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (software_timers[i].id == 0) {
            software_timers[i].id = next_timer_id++;
            uint32_t ticks_needed = ms / (1000 / freq);
            if (ticks_needed == 0)
                ticks_needed = 1;

            software_timers[i].expires_at = ticks + ticks_needed;
            software_timers[i].interval = is_interval ? ticks_needed : 0;
            software_timers[i].callback = callback;
            return software_timers[i].id;
        }
    }
    return -1;
}

int set_timeout(uint32_t ms, TimerCallback callback) {
    return add_timer(ms, callback, 0);
}

int set_interval(uint32_t ms, TimerCallback callback) {
    return add_timer(ms, callback, 1);
}

void cancel_timer(int id) {
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (software_timers[i].id == id) {
            software_timers[i].id = 0;
            return;
        }
    }
}