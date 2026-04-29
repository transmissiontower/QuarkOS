; SPDX-License-Identifier: MIT
;
; src/boot/boot.s - x86 Multiboot entry point, initial paging setup, and jump to higher-half kernel

MBOOT_PAGE_ALIGN EQU 1 << 0
MBOOT_MEM_INFO   EQU 1 << 1
MBOOT_VIDEO_MODE EQU 1 << 2
MBOOT_MAGIC      EQU 0x1BADB002
MBOOT_FLAGS      EQU MBOOT_PAGE_ALIGN | MBOOT_MEM_INFO | MBOOT_VIDEO_MODE
MBOOT_CHECKSUM   EQU -(MBOOT_MAGIC + MBOOT_FLAGS)

section .multiboot
align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECKSUM
    dd 0, 0, 0, 0, 0
    dd 0
    dd 1920
    dd 1080
    dd 32

section .bss
align 16
stack_bottom:
    resb 16384 * 8
stack_top:

section .boot
global _start
global stack_top
extern kmain

_start:
    mov esp, (stack_top - 0xC0000000)
    mov ecx, (initial_page_dir - 0xC0000000)
    mov cr3, ecx
    mov ecx, cr4
    or ecx, 0x00000010
    mov cr4, ecx
    mov ecx, cr0
    or ecx, 0x80000000
    mov cr0, ecx
    lea ecx, [higher_half]
    jmp ecx

section .text
higher_half:
    mov esp, stack_top
    push ebx
    push eax
    xor ebp, ebp
    call kmain
.halt:
    hlt
    jmp .halt

section .data
align 4096
global initial_page_dir
initial_page_dir:
    dd 0x00000083
    dd 0x00400083
    dd 0x00800083
    dd 0x00C00083
    times 764 dd 0
    dd 0x00000083
    dd 0x00400083
    dd 0x00800083
    dd 0x00C00083
    times 256-4 dd 0
