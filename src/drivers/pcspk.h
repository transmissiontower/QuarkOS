#ifndef PCSPK_H
#define PCSPK_H

#include "lib/stdint.h"

void drv_pcspk_play(uint32_t freq);
void drv_pcspk_stop();
void drv_pcspk_beep(uint32_t freq, uint32_t ms);

/* PWM audio — driven from the PIT CH0 ISR */
void drv_pcspk_pwm_start(const uint8_t *buf, uint32_t len, uint32_t rate);
int  drv_pcspk_pwm_active(void);
int  drv_pcspk_pwm_ready(void);
void drv_pcspk_pwm_tick(void);   /* called once per PIT IRQ during playback */

/* Shared counters used by timer.c to keep ticks rate-correct during PWM */
extern volatile uint32_t drv_pcspk_pwm_subtick;
extern volatile uint32_t drv_pcspk_pwm_subtick_max;

#endif
