#include "kernel/crash.h"
#include "drivers/acpi.h"
#include "util/util.h"

#define SCANCODE_R 0x13

void crash_wait_for_reboot(void) {
    asm volatile("cli");
    while (1) {
        uint8_t status = (uint8_t)inb(0x64);
        if (status & 0x01) {
            uint8_t scancode = (uint8_t)inb(0x60);
            if (!(scancode & 0x80) && (scancode & 0x7F) == SCANCODE_R) {
                acpi_reboot();
            }
        }
    }
}
