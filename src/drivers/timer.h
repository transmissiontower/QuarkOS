#ifndef TIMER_H
#define TIMER_H

#include "lib/stdint.h"
#include "util/util.h"

typedef void (*TimerCallback)();

typedef struct {
    int id;
    uint64_t expires_at;
    uint32_t interval;
    TimerCallback callback;
} Timer;

void timer_init();
void on_irq_0(struct InterruptRegisters *regs);

uint64_t get_uptime_ms();
void sleep(uint32_t ms);

int set_timeout(uint32_t ms, TimerCallback callback);
int set_interval(uint32_t ms, TimerCallback callback);
void cancel_timer(int id);

#endif