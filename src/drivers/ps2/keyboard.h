// SPDX-License-Identifier: MIT
/*
 * src/drivers/ps2/keyboard.h - PS/2 keyboard driver interface
 */
#ifndef KEYBOARD_H
#define KEYBOARD_H
#include "arch/x86/idt.h"
void drv_input_keyboard_init();
void keyboard_handler(struct InterruptRegisters *regs);
void keyboard_poll();
#endif
