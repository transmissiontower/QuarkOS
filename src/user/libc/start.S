.section .text.start, "ax"
.global _start
.type _start, %function
.extern main

_start:
    bl      main
    mov     x1, x0
    mov     x0, #0
    svc     #0
1:
    b       1b
.size _start, . - _start
