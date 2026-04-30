// SPDX-License-Identifier: MIT
/*
 * src/drivers/pcspk.c - PC speaker tone generation and PWM audio playback via PIT channel 2
 */
#include "drivers/pcspk.h"
#include "drivers/timer.h"
#include "util/util.h"
#define PIT_BASE_FREQ 1193180
#define PIT_CMD 0x43
#define PIT_CH2 0x42
#define PCSPK_PORT 0x61
void drv_pcspk_play(uint32_t freq) {
    if (freq == 0) {
        drv_pcspk_stop();
        return;
    }
    uint32_t divisor = PIT_BASE_FREQ / freq;
    outb(PIT_CMD, 0xB6);
    outb(PIT_CH2, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH2, (uint8_t)(divisor >> 8));
    uint8_t state = inb(PCSPK_PORT);
    outb(PCSPK_PORT, state | 0x03);
}
void drv_pcspk_stop() {
    uint8_t state = inb(PCSPK_PORT);
    outb(PCSPK_PORT, state & ~0x03);
}
void drv_pcspk_beep(uint32_t freq, uint32_t ms) {
    drv_pcspk_play(freq);
    sleep(ms);
    drv_pcspk_stop();
}
#define SYS_TICK_HZ 100
static volatile const uint8_t *pwm_buf = 0;
static volatile uint32_t pwm_len = 0;
static volatile uint32_t pwm_pos = 0;
volatile uint32_t drv_pcspk_pwm_subtick = 0;
volatile uint32_t drv_pcspk_pwm_subtick_max = 0;
void drv_pcspk_pwm_start(const uint8_t *buf, uint32_t len, uint32_t rate) {
    if (len == 0 || rate == 0)
        return;
    outb(PIT_CMD, 0xB6);
    outb(PIT_CH2, 2);
    outb(PIT_CH2, 0);
    uint8_t state = inb(PCSPK_PORT);
    outb(PCSPK_PORT, (state | 0x01) & ~0x02);
    pwm_buf = buf;
    pwm_len = len;
    pwm_pos = 0;
    drv_pcspk_pwm_subtick_max = rate / SYS_TICK_HZ;
    if (drv_pcspk_pwm_subtick_max == 0)
        drv_pcspk_pwm_subtick_max = 1;
    drv_pcspk_pwm_subtick = 0;
    uint32_t divisor = PIT_BASE_FREQ / rate;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)(divisor >> 8));
}
int drv_pcspk_pwm_active(void) {
    return pwm_buf != 0 && pwm_pos < pwm_len;
}
int drv_pcspk_pwm_ready(void) {
    return pwm_buf == 0 || pwm_pos >= pwm_len;
}
void drv_pcspk_pwm_tick(void) {
    if (pwm_pos >= pwm_len) {
        uint8_t state = inb(PCSPK_PORT);
        outb(PCSPK_PORT, state & ~0x03);
        pwm_buf = 0;
        uint32_t div = PIT_BASE_FREQ / SYS_TICK_HZ;
        outb(0x43, 0x36);
        outb(0x40, (uint8_t)(div & 0xFF));
        outb(0x40, (uint8_t)(div >> 8));
        return;
    }
    static uint32_t acc = 0;
    uint8_t s = pwm_buf[pwm_pos++];
    acc += s;
    uint8_t p = inb(PCSPK_PORT) | 0x01;
    if (acc >= 128) {
        outb(PCSPK_PORT, p | 0x02);
        acc -= 256;
    } else {
        outb(PCSPK_PORT, p & ~0x02);
    }
}
