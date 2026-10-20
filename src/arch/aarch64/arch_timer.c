#include "arch/aarch64/platform.h"
#include "kernel/lib/util.h"

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

void arm_timer_init(void) {
    uint32_t freq = read_cntfrq();
    if (freq == 0)
        freq = 62500000;

    timer_reload = freq / TIMER_HZ;

    write_cntp_ctl(0);
    write_cntp_tval(timer_reload);
    write_cntp_ctl(1);

    arm_gic_enable_irq(ARM_IRQ_TIMER);

    arm_uart_puts("  Physical generic timer armed (AArch64)\n");
}

void arm_timer_clear_irq(void) {
    write_cntp_tval(timer_reload);
}
