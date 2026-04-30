// SPDX-License-Identifier: MIT
/*
 * src/arch/aarch64/stubs.c - Stub implementations for x86-specific interfaces on AArch64
 */
#include "lib/stdint.h"
#include "util/util.h"

void arch_gdt_init(void) {
}
void arch_tss_set_stack(uintptr_t kernel_stack) {
    (void)kernel_stack;
}

void arch_idt_init(void) {
}

void arch_fpu_enable(void) {
    uint64_t cpacr;
    asm volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr |= (3UL << 20);
    asm volatile("msr cpacr_el1, %0" ::"r"(cpacr));
    asm volatile("isb");
}

int acpi_init(void) {
    return 1;
}
int acpi_get_cpu_count(void) {
    return 1;
}
int acpi_power_button_available(void) {
    return 1;
}
int acpi_reset_available(void) {
    return 1;
}
int acpi_power_ready(void) {
    return 1;
}

void acpi_shutdown(void) {
    extern void arm_uart_puts(const char *s);
    arm_uart_puts("System shutdown requested (PSCI)\n");

    register uint64_t x0 asm("x0") = 0x84000008;
    asm volatile("hvc #0" ::"r"(x0));
    while (1)
        asm volatile("wfi");
}

void acpi_reboot(void) {
    register uint64_t x0 asm("x0") = 0x84000009;
    asm volatile("hvc #0" ::"r"(x0));
    while (1)
        asm volatile("wfi");
}

int drv_sb16_init(void) {
    return -1;
}
int drv_sb16_play(const void *buf, uint32_t len, uint32_t rate, uint8_t channels, uint8_t bits) {
    (void)buf;
    (void)len;
    (void)rate;
    (void)channels;
    (void)bits;
    return -1;
}
int drv_sb16_ready(void) {
    return 0;
}

void drv_pcspk_play(uint32_t freq) {
    (void)freq;
}
void drv_pcspk_stop(void) {
}
int drv_pcspk_pwm_active(void) {
    return 0;
}
void drv_pcspk_pwm_tick(void) {
}
void drv_pcspk_pwm_start(const uint8_t *buf, uint32_t len, uint32_t rate) {
    (void)buf;
    (void)len;
    (void)rate;
}
int drv_pcspk_pwm_ready(void) {
    return 1;
}
volatile uint32_t drv_pcspk_pwm_subtick = 0;
volatile uint32_t drv_pcspk_pwm_subtick_max = 1;

typedef struct {
    uint8_t second, minute, hour, day, month;
    uint16_t year;
} Time;
void drv_rtc_read_datetime(Time *t) {
    volatile uint32_t *rtc_dr = (volatile uint32_t *)0xF5010000UL;
    uint32_t epoch = *rtc_dr;

    uint32_t secs = epoch;
    uint32_t days = secs / 86400;
    uint32_t rem = secs % 86400;
    t->hour = (uint8_t)(rem / 3600);
    t->minute = (uint8_t)((rem % 3600) / 60);
    t->second = (uint8_t)(rem % 60);

    uint16_t year = 1970;
    for (;;) {
        uint32_t ydays = 365;
        if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)
            ydays = 366;
        if (days < ydays)
            break;
        days -= ydays;
        year++;
    }
    t->year = year;

    static const uint8_t mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    uint8_t mon = 0;
    for (; mon < 12; mon++) {
        uint32_t md = mdays[mon];
        if (mon == 1 && leap)
            md = 29;
        if (days < md)
            break;
        days -= md;
    }
    t->month = mon + 1;
    t->day = (uint8_t)(days + 1);
}

void drv_stor_ata_init(void) {
}
void drv_stor_ata_read(uint32_t lba, uint8_t *buffer) {
    (void)lba;
    (void)buffer;
}
void drv_stor_ata_write(uint32_t lba, uint8_t *buffer) {
    (void)lba;
    (void)buffer;
}

void drv_input_keyboard_init(void) {
}
void drv_input_mouse_init(void) {
}

void crash_wait_for_reboot(void) {
    asm volatile("msr daifset, #2");
    while (1)
        asm volatile("wfi");
}
