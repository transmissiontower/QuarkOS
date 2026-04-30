// SPDX-License-Identifier: MIT
/*
 * src/drivers/timer.c - PIT-based system tick, sleep, and software timer management
 */
#include "drivers/timer.h"
#include "drivers/pcspk.h"
void irq_install_handler(int irq, void (*handler)(struct InterruptRegisters *r));
#include "lib/stdint.h"
#include "util/util.h"
#include "video/vga.h"
volatile uint64_t ticks = 0;
const uint32_t freq = 100;
#define MAX_TIMERS 16
static Timer software_timers[MAX_TIMERS];
static int next_timer_id = 1;
extern void k_tick_handler();
void on_irq_0(struct InterruptRegisters *regs) {
    (void)regs;
    extern void arm_timer_clear_irq(void);
    arm_timer_clear_irq();
    if (drv_pcspk_pwm_active()) {
        drv_pcspk_pwm_tick();
        drv_pcspk_pwm_subtick++;
        if (drv_pcspk_pwm_subtick < drv_pcspk_pwm_subtick_max)
            return;
        drv_pcspk_pwm_subtick = 0;
    }
    ticks++;
    k_tick_handler();
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
void drv_pit_init() {
    ticks = 0;
    for (int i = 0; i < MAX_TIMERS; i++)
        software_timers[i].id = 0;
    irq_install_handler(0, &on_irq_0);
}
uint64_t get_uptime_ms() {
    return ticks * (1000 / freq);
}
void sleep(uint32_t ms) {
    uint64_t end_time = ticks + (ms / (1000 / freq));
    while (ticks < end_time) {
        asm volatile("msr daifclr, #2" ::: "memory");
        asm volatile("wfi");
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
