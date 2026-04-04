#ifndef KERNEL_BOOTSPLASH_H
#define KERNEL_BOOTSPLASH_H

#include "arch/x86/multiboot.h"
#include "drivers/rtc.h"
#include "lib/stdbool.h"
#include "lib/stdint.h"

void boot_log_record_boot_info(struct multiboot_info *boot_info);
int boot_log_add(const char *message);
void boot_log_replace(int index, const char *message);
void boot_log_add_dec(const char *label, uint32_t value, const char *suffix);
void boot_log_add_hex(const char *label, uint32_t value);
void boot_log_add_framebuffer_info(void);
void boot_log_add_rtc_line(const Time *t);
void boot_log_add_acpi_capabilities(bool power_button_available, bool reset_capable);
void draw_boot_splash(void);

#endif
