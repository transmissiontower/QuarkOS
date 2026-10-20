#ifndef TIMER_H
#define TIMER_H
#include "stdint.h"
#include "kernel/lib/util.h"
#include "kernel/core/tick.h"
void drv_timer_init(void);
void on_arm_timer_irq(struct InterruptRegisters *regs);
#endif
