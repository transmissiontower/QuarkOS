[BITS 32]
global _start
extern main
extern qk_exit

section .text
_start:
    call main
    push eax
    call qk_exit
    jmp $