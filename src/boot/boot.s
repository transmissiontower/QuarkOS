MBOOT_PAGE_ALIGN EQU 1 << 0
MBOOT_MEM_INFO EQU 1 << 1
MBOOT_VIDEO_MODE EQU 1 << 2

MBOOT_MAGIC EQU 0x1BADB002
MBOOT_FLAGS EQU MBOOT_PAGE_ALIGN | MBOOT_MEM_INFO | MBOOT_VIDEO_MODE
MBOOT_CHECKSUM EQU -(MBOOT_MAGIC + MBOOT_FLAGS)

section .multiboot
ALIGN 4
    DD MBOOT_MAGIC
    DD MBOOT_FLAGS
    DD MBOOT_CHECKSUM
    DD 0, 0, 0, 0, 0

    DD 0
    DD 1024
    DD 768
    DD 32

SECTION .bss 
ALIGN 16
stack_bottom:
    RESB 16384 * 8
stack_top:

section .boot

global _start
global stack_top;

_start:
    MOV esp, (initial_page_dir - 0xC0000000)
    MOV ecx, (initial_page_dir - 0xC0000000)
    MOV cr3, ecx

    MOV ecx, cr4
    OR ecx, 0x10
    MOV cr4, ecx

    MOV ecx, cr0
    OR ecx, 0x80000000
    MOV cr0, ecx

    LEA ecx, [higher_half]
    JMP ecx

section .text
higher_half:
    MOV esp, stack_top
    PUSH ebx
    PUSH eax
    XOR ebp, ebp
    extern kmain
    CALL kmain

halt:
    hlt
    JMP halt


section .data
align 4096
global initial_page_dir
initial_page_dir:
    DD 10000011b

    DD (1 << 22) | 10000011b 

    TIMES 768-2 DD 0

    DD(0 << 22) | 10000011b 
    DD(1 << 22) | 10000011b 
    DD(2 << 22) | 10000011b
    DD(3 << 22) | 10000011b
    
    TIMES 1024 - (768 + 4) DD 0 



