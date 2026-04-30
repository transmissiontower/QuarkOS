// SPDX-License-Identifier: MIT
/*
 * src/drivers/pcspk.h - PC speaker driver interface for tones and PWM audio
 */
#ifndef PCSPK_H
#define PCSPK_H
#include "lib/stdint.h"
void drv_pcspk_play(uint32_t freq);
void drv_pcspk_stop();
void drv_pcspk_beep(uint32_t freq, uint32_t ms);
void drv_pcspk_pwm_start(const uint8_t *buf, uint32_t len, uint32_t rate);
int drv_pcspk_pwm_active(void);
int drv_pcspk_pwm_ready(void);
void drv_pcspk_pwm_tick(void);
extern volatile uint32_t drv_pcspk_pwm_subtick;
extern volatile uint32_t drv_pcspk_pwm_subtick_max;
#endif
