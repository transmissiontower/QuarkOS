// SPDX-License-Identifier: MIT
/*
 * src/drivers/timer.c - System tick, sleep, and software timer management
 */
#include "drivers/timer.h"
#include "arch/aarch64/platform.h"
#include "lib/stdint.h"
#include "util/util.h"
volatile uint64_t ticks = 0;
const uint32_t freq = 100;
extern void k_tick_handler();
void on_arm_timer_irq(struct InterruptRegisters *regs) {
    (void)regs;
    extern void arm_timer_clear_irq(void);
    arm_timer_clear_irq();
    ticks++;
    k_tick_handler();
}
void drv_timer_init() {
    ticks = 0;
    arm_gic_install_irq_handler(ARM_IRQ_TIMER, &on_arm_timer_irq);
    arm_gic_install_irq_handler(ARM_IRQ_VTIMER, &on_arm_timer_irq);
}
uint64_t get_uptime_ms() {
    return ticks * (1000 / freq);
}
