/* SPDX-License-Identifier: MIT */
ENTRY(_start)
OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)

PHDRS {
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    . = 0xC0010000;

    .boot ALIGN(4K) : AT(ADDR(.boot) - 0xC0000000 + 0x40000000) {
        *(.boot)
    } :text

    .vectors ALIGN(4K) : AT(ADDR(.vectors) - 0xC0000000 + 0x40000000) {
        *(.vectors)
    } :text

    .text ALIGN(4K) : AT(ADDR(.text) - 0xC0000000 + 0x40000000) {
        *(.text)
    } :text

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - 0xC0000000 + 0x40000000) {
        *(.rodata*)
    } :text

    .data ALIGN(4K) : AT(ADDR(.data) - 0xC0000000 + 0x40000000) {
        *(.data)
    } :data

    .modules ALIGN(4K) : AT(ADDR(.modules) - 0xC0000000 + 0x40000000) {
        *(.modules)
    } :data

    _bss_start = .;
    .bss ALIGN(4K) : AT(ADDR(.bss) - 0xC0000000 + 0x40000000) {
        *(COMMON)
        *(.bss)
    } :data
    _bss_end = .;

    . = ALIGN(4096);
    _kernel_end = .;
    kernel_end = .;
}
