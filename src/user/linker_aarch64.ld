OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x400000;

    .text : {
        *(.text)
    } :text

    .rodata : {
        *(.rodata*)
    } :text

    . = ALIGN(0x1000);

    .data : {
        *(.data)
    } :data

    . = ALIGN(0x1000);

    .bss : {
        *(.bss)
        *(COMMON)
    } :data
}
