#ifndef SB16_H
#define SB16_H

#include "lib/stdint.h"

int  drv_sb16_init(void);
int  drv_sb16_play(const void *virt_buf, uint32_t bytes,
                   uint32_t rate, uint8_t channels, uint8_t bits);
int  drv_sb16_ready(void);

#endif
