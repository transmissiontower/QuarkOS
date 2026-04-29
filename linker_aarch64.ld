/* SPDX-License-Identifier: MIT */
/*
 * linker_aarch64.ld - AArch64 kernel linker script with higher-half mapping at 0xC0000000
 */
ENTRY(_start)
OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)

SECTIONS {
    . = 0xC0010000;

    .boot ALIGN(4K) : AT(ADDR(.boot) - 0xC0000000 + 0x40000000) {
        *(.boot)
    }

    .vectors ALIGN(4K) : AT(ADDR(.vectors) - 0xC0000000 + 0x40000000) {
        *(.vectors)
    }

    .text ALIGN(4K) : AT(ADDR(.text) - 0xC0000000 + 0x40000000) {
        *(.text)
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - 0xC0000000 + 0x40000000) {
        *(.rodata*)
    }

    .data ALIGN(4K) : AT(ADDR(.data) - 0xC0000000 + 0x40000000) {
        *(.data)
    }

    .modules ALIGN(4K) : AT(ADDR(.modules) - 0xC0000000 + 0x40000000) {
        *(.modules)
    }

    _bss_start = .;
    .bss ALIGN(4K) : AT(ADDR(.bss) - 0xC0000000 + 0x40000000) {
        *(COMMON)
        *(.bss)
    }
    _bss_end = .;

    . = ALIGN(4096);
    _kernel_end = .;
    kernel_end = .;
}
