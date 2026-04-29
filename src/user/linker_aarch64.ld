OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .text : {
        *(.text)
    }

    . = ALIGN(0x1000);

    .data : {
        *(.data)
        *(.rodata*)
    }

    . = ALIGN(0x1000);

    .bss : {
        *(.bss)
        *(COMMON)
    }
}
