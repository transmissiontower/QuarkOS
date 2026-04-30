// SPDX-License-Identifier: MIT
/*
 * src/arch/aarch64/timer_arm.c - ARM generic timer driver for preemptive scheduling ticks
 */
#include "arch/aarch64/platform.h"
#include "util/util.h"

#define TIMER_HZ 100

static uint32_t timer_reload;

static inline uint32_t read_cntfrq(void) {
    uint64_t val;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return (uint32_t)val;
}

static inline void write_cntp_tval(uint32_t val) {
    uint64_t v = val;
    asm volatile("msr cntp_tval_el0, %0" ::"r"(v));
}

static inline void write_cntp_ctl(uint32_t val) {
    uint64_t v = val;
    asm volatile("msr cntp_ctl_el0, %0" ::"r"(v));
}

static inline void write_cntv_tval(uint32_t val) {
    uint64_t v = val;
    asm volatile("msr cntv_tval_el0, %0" ::"r"(v));
}

static inline void write_cntv_ctl(uint32_t val) {
    uint64_t v = val;
    asm volatile("msr cntv_ctl_el0, %0" ::"r"(v));
}

void arm_timer_init(void) {
    uint32_t freq = read_cntfrq();
    if (freq == 0)
        freq = 62500000;

    timer_reload = freq / TIMER_HZ;

    write_cntv_ctl(0);
    write_cntv_tval(timer_reload);
    write_cntv_ctl(1);

    mmio_write32(GIC_DIST_VIRT + GICD_ISENABLER, (1u << 27) | (1u << 30));

    arm_uart_puts("  Generic timer armed (AArch64)\n");
}

void arm_timer_clear_irq(void) {
    write_cntv_tval(timer_reload);
}
