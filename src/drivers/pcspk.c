#include "drivers/pcspk.h"
#include "drivers/timer.h"
#include "util/util.h"

#define PIT_BASE_FREQ   1193180
#define PIT_CMD         0x43
#define PIT_CH2         0x42
#define PCSPK_PORT      0x61

void drv_pcspk_play(uint32_t freq)
{
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

void drv_pcspk_stop()
{
    uint8_t state = inb(PCSPK_PORT);
    outb(PCSPK_PORT, state & ~0x03);
}

void drv_pcspk_beep(uint32_t freq, uint32_t ms)
{
    drv_pcspk_play(freq);
    sleep(ms);
    drv_pcspk_stop();
}

/* ------------------------------------------------------------------ */
/* PWM audio via PIT CH0                                               */
/*                                                                     */
/* The caller reprograms PIT CH0 to fire at `rate` Hz.  Each IRQ call */
/* drv_pcspk_pwm_tick() which reads the next sample and sets/clears   */
/* port 0x61 bit 1 directly (bypassing PIT CH2 frequency control).    */
/* When the chunk is exhausted PIT CH0 is restored to SYS_TICK_HZ.    */
/* ------------------------------------------------------------------ */
#define SYS_TICK_HZ 100   /* must match timer.c `freq` */

static volatile const uint8_t *pwm_buf         = 0;
static volatile uint32_t       pwm_len         = 0;
static volatile uint32_t       pwm_pos         = 0;
/* Both counters are accessed by timer.c via the externs declared in pcspk.h */
volatile uint32_t drv_pcspk_pwm_subtick     = 0;
volatile uint32_t drv_pcspk_pwm_subtick_max = 0;   /* = rate / SYS_TICK_HZ */

void drv_pcspk_pwm_start(const uint8_t *buf, uint32_t len, uint32_t rate)
{
    if (len == 0 || rate == 0)
        return;

    /*
     * Set PIT CH2 to a fixed ultrasonic frequency (divisor=2 ≈ 596 kHz).
     * At that speed its square-wave output oscillates far above hearing and
     * QEMU treats bit 1 (speaker-data) as a direct amplitude control.
     * Keeping bit 0 (PIT CH2 gate) = 1 is required so QEMU's pcspk driver
     * activates at all — it only produces audio when both bit 0 and bit 1
     * of port 0x61 are set.
     */
    outb(PIT_CMD, 0xB6);               /* CH2, LSB/MSB, mode 3 */
    outb(PIT_CH2, 2);                  /* divisor LSB */
    outb(PIT_CH2, 0);                  /* divisor MSB */
    uint8_t state = inb(PCSPK_PORT);
    outb(PCSPK_PORT, (state | 0x01) & ~0x02); /* bit0=1 (gate on), bit1=0 (silent) */

    pwm_buf         = buf;
    pwm_len         = len;
    pwm_pos         = 0;
    drv_pcspk_pwm_subtick_max = rate / SYS_TICK_HZ;
    if (drv_pcspk_pwm_subtick_max == 0)
        drv_pcspk_pwm_subtick_max = 1;
    drv_pcspk_pwm_subtick = 0;

    /* Reprogram PIT CH0 to fire at `rate` Hz */
    uint32_t divisor = PIT_BASE_FREQ / rate;
    outb(0x43, 0x36);                           /* CH0, LSB/MSB, mode 3 */
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)(divisor >> 8));
}

int drv_pcspk_pwm_active(void)
{
    return pwm_buf != 0 && pwm_pos < pwm_len;
}

int drv_pcspk_pwm_ready(void)
{
    return pwm_buf == 0 || pwm_pos >= pwm_len;
}

void drv_pcspk_pwm_tick(void)
{
    if (pwm_pos >= pwm_len) {
        /* Chunk exhausted — silence speaker and restore PIT CH0 */
        uint8_t state = inb(PCSPK_PORT);
        outb(PCSPK_PORT, state & ~0x03);        /* speaker off, gate off */
        pwm_buf = 0;

        uint32_t div = PIT_BASE_FREQ / SYS_TICK_HZ;
        outb(0x43, 0x36);
        outb(0x40, (uint8_t)(div & 0xFF));
        outb(0x40, (uint8_t)(div >> 8));
        return;
    }

    /* 1-bit PDM: sigma-delta accumulator (error in 0..255 range) */
    static uint32_t acc = 0;
    uint8_t s = pwm_buf[pwm_pos++];
    acc += s;
    /* Always keep bit 0 (PIT CH2 gate) set — QEMU needs it to emit audio */
    uint8_t p = inb(PCSPK_PORT) | 0x01;
    if (acc >= 128) {
        outb(PCSPK_PORT, p | 0x02);   /* output 1: gate on + speaker on */
        acc -= 256;
    } else {
        outb(PCSPK_PORT, p & ~0x02);  /* output 0: gate on + speaker off */
    }
}
