#include "drivers/ps2/mouse.h"
#include "arch/x86/idt.h"
#include "util/util.h"

void drv_input_mouse_wait(uint8_t type)
{
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout--) {
            if ((inb(0x64) & 1) == 1)
                return;
        }
        return;
    } else {
        while (timeout--) {
            if ((inb(0x64) & 2) == 0)
                return;
        }
        return;
    }
}

void mouse_write(uint8_t a_write)
{
    drv_input_mouse_wait(1);
    outb(0x64, 0xD4);
    drv_input_mouse_wait(1);
    outb(0x60, a_write);
}

uint8_t mouse_read()
{
    drv_input_mouse_wait(0);
    return (uint8_t)inb(0x60);
}

void mouse_handler(struct InterruptRegisters *regs)
{
    (void)regs;
    extern void process_wake_on_irq(int irq);
    process_wake_on_irq(12);
}

void drv_input_mouse_init()
{
    uint8_t status;

    drv_input_mouse_wait(1);
    outb(0x64, 0xA8);

    drv_input_mouse_wait(1);
    outb(0x64, 0x20);
    drv_input_mouse_wait(0);
    status = (inb(0x60) | 2) & ~0x20;

    drv_input_mouse_wait(1);
    outb(0x64, 0x60);
    drv_input_mouse_wait(1);
    outb(0x60, status);

    mouse_write(0xF6);
    mouse_read();

    mouse_write(0xF4);
    mouse_read();

    irq_install_handler(12, mouse_handler);
}
