; SPDX-License-Identifier: MIT
;
; src/user/lib/crt0.s - User-space C runtime startup stub

[BITS 32]
global _start
extern main
extern qk_sys_exit

section .text
_start:
    call main
    push eax
    call qk_sys_exit
    jmp $
