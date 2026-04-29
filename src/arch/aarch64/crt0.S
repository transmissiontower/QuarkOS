// SPDX-License-Identifier: MIT
/*
 * src/arch/aarch64/crt0.S - C runtime startup stub for user-space programs
 */
.section .text
.global _start
.extern main
.extern qk_sys_exit

_start:
    bl      main
    mov     x0, x0
    bl      qk_sys_exit
    b       .
