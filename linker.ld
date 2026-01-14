ENTRY(_start)

SECTIONS {
    . = 0xC0400000;

    .multiboot ALIGN(4K) : AT(ADDR(.multiboot) - 0xC0000000) {
        *(.multiboot)
    }

    .boot ALIGN(4K) : AT(ADDR(.boot) - 0xC0000000) {
        *(.boot)
    }

    .text ALIGN(4K) : AT(ADDR(.text) - 0xC0000000) {
        *(.text)
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - 0xC0000000) {
        *(.rodata)
    }

    .data ALIGN(4K) : AT(ADDR(.data) - 0xC0000000) {
        *(.data)
    }

    .bss ALIGN(4K) : AT(ADDR(.bss) - 0xC0000000) {
        *(COMMON)
        *(.bss)
    }
    . = ALIGN(4096);
    
    _kernel_end = .;
    kernel_end = .;
}
