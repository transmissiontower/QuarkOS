#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "arch/x86/idt.h"

void keyboard_init();
void keyboard_handler(struct InterruptRegisters *regs);
void keyboard_poll();

#endif