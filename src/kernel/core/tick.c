#include "kernel/core/tick.h"

static volatile uint64_t tick_count;
extern void k_tick_handler(void);

void tick_handler(void) {
    tick_count++;
    k_tick_handler();
}

uint64_t uptime_ms(void) {
    return tick_count * (1000 / TICK_HZ);
}

uint64_t get_uptime_ms(void) {
    return uptime_ms();
}
