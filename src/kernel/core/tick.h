#ifndef SODIUM_KERNEL_TICK_H
#define SODIUM_KERNEL_TICK_H
#include "stdint.h"
#define TICK_HZ 100
void tick_handler(void);
uint64_t uptime_ms(void);
uint64_t get_uptime_ms(void);
#endif
