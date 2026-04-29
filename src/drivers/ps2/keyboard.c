// SPDX-License-Identifier: MIT
/*
 * src/drivers/ps2/keyboard.c - PS/2 keyboard IRQ handler and initialization
 */
#include "drivers/ps2/keyboard.h"
#include "arch/x86/idt.h"
#include "util/util.h"
void keyboard_handler(struct InterruptRegisters *regs)
{
    (void)regs;
    extern void process_wake_on_irq(int irq);
    process_wake_on_irq(1);
}
void drv_input_keyboard_init()
{
    irq_install_handler(1, keyboard_handler);
}
