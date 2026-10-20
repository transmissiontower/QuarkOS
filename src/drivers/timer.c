#include "drivers/timer.h"
#include "arch/aarch64/platform.h"
#include "stdint.h"
#include "kernel/lib/util.h"
#include "kernel/core/tick.h"
void on_arm_timer_irq(struct InterruptRegisters *regs) {
    (void)regs;
    extern void arm_timer_clear_irq(void);
    arm_timer_clear_irq();
    tick_handler();
}
void drv_timer_init() {
    arm_gic_install_irq_handler(ARM_IRQ_TIMER, &on_arm_timer_irq);
    arm_gic_install_irq_handler(ARM_IRQ_VTIMER, &on_arm_timer_irq);
}
