// SPDX-License-Identifier: MIT
/*
 * src/drivers/timer.h - System timer interface for sleep, uptime, and scheduled callbacks
 */
#ifndef TIMER_H
#define TIMER_H
#include "lib/stdint.h"
#include "util/util.h"
void drv_timer_init(void);
void on_arm_timer_irq(struct InterruptRegisters *regs);
uint64_t get_uptime_ms();
#endif
