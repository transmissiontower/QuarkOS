// SPDX-License-Identifier: MIT
/*
 * src/arch/aarch64/uart.c - PL011 UART serial console output
 */
#include "arch/aarch64/platform.h"

void arm_uart_putc(char c) {
    while (mmio_read32(UART0_VIRT + UART_FR) & UART_FR_TXFF)
        ;
    mmio_write8(UART0_VIRT + UART_DR, (uint8_t)c);
}

void arm_uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n')
            arm_uart_putc('\r');
        arm_uart_putc(*s++);
    }
}
