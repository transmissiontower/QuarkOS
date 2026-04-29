// SPDX-License-Identifier: MIT
/*
 * src/arch/aarch64/vectors.S - AArch64 exception vector table and trap handlers
 */
.extern k_syscall_entry
.extern k_syscall_dispatch_aarch64
.extern arm_irq_dispatch
.extern arm_dabt_handler_c
.extern arm_uart_puts
.extern arm_el0_unknown_exception

.section .vectors, "ax"
.balign 0x800
.global arm_vector_table
arm_vector_table:

.balign 0x80
    b       _el1_sp0_sync_hang

.balign 0x80
    b       _el1_sp0_irq_hang

.balign 0x80
    b       _el1_sp0_fiq_hang

.balign 0x80
    b       _el1_sp0_serror_hang

.balign 0x80
    b       _el1_sync_handler

.balign 0x80
    b       _el1_irq_handler

.balign 0x80
    b       _el1_fiq_hang

.balign 0x80
    b       _el1_serror_hang

.balign 0x80
    b       _el0_sync_handler

.balign 0x80
    b       _el0_irq_handler

.balign 0x80
    b       _el0_fiq_hang

.balign 0x80
    b       _el0_serror_hang

.balign 0x80
    b       .

.balign 0x80
    b       .

.balign 0x80
    b       .

.balign 0x80
    b       .

.section .text

_el1_sp0_sync_hang:
_el1_sp0_irq_hang:
_el1_sp0_fiq_hang:
_el1_sp0_serror_hang:
_el1_fiq_hang:
_el1_serror_hang:
_el0_fiq_hang:
_el0_serror_hang:
    b       .

.global arm_install_vectors
arm_install_vectors:
    ldr     x0, =arm_vector_table
    msr     VBAR_EL1, x0
    isb
    ret

.macro save_el0_context

    sub     sp, sp, #0x120

    stp     x0,  x1,  [sp, #0x000]
    stp     x2,  x3,  [sp, #0x010]
    stp     x4,  x5,  [sp, #0x020]
    stp     x6,  x7,  [sp, #0x030]
    stp     x8,  x9,  [sp, #0x040]
    stp     x10, x11, [sp, #0x050]
    stp     x12, x13, [sp, #0x060]
    stp     x14, x15, [sp, #0x070]
    stp     x16, x17, [sp, #0x080]
    stp     x18, x19, [sp, #0x090]
    stp     x20, x21, [sp, #0x0A0]
    stp     x22, x23, [sp, #0x0B0]
    stp     x24, x25, [sp, #0x0C0]
    stp     x26, x27, [sp, #0x0D0]
    stp     x28, x29, [sp, #0x0E0]

    str     x30,       [sp, #0x0F0]

    mrs     x10, SP_EL0
    str     x10,       [sp, #0x0F8]

    mrs     x10, ELR_EL1
    str     x10,       [sp, #0x100]

    mrs     x10, SPSR_EL1
    str     x10,       [sp, #0x108]

.endm

.macro restore_el0_context

    ldr     x10,       [sp, #0x108]
    msr     SPSR_EL1, x10

    ldr     x10,       [sp, #0x100]
    msr     ELR_EL1, x10

    ldr     x10,       [sp, #0x0F8]
    msr     SP_EL0, x10

    ldr     x30,       [sp, #0x0F0]

    ldp     x28, x29, [sp, #0x0E0]
    ldp     x26, x27, [sp, #0x0D0]
    ldp     x24, x25, [sp, #0x0C0]
    ldp     x22, x23, [sp, #0x0B0]
    ldp     x20, x21, [sp, #0x0A0]
    ldp     x18, x19, [sp, #0x090]
    ldp     x16, x17, [sp, #0x080]
    ldp     x14, x15, [sp, #0x070]
    ldp     x12, x13, [sp, #0x060]
    ldp     x10, x11, [sp, #0x050]
    ldp     x8,  x9,  [sp, #0x040]
    ldp     x6,  x7,  [sp, #0x030]
    ldp     x4,  x5,  [sp, #0x020]
    ldp     x2,  x3,  [sp, #0x010]
    ldp     x0,  x1,  [sp, #0x000]

    add     sp, sp, #0x120
.endm

.macro save_el1_context
    sub     sp, sp, #0x110

    stp     x0,  x1,  [sp, #0x000]
    stp     x2,  x3,  [sp, #0x010]
    stp     x4,  x5,  [sp, #0x020]
    stp     x6,  x7,  [sp, #0x030]
    stp     x8,  x9,  [sp, #0x040]
    stp     x10, x11, [sp, #0x050]
    stp     x12, x13, [sp, #0x060]
    stp     x14, x15, [sp, #0x070]
    stp     x16, x17, [sp, #0x080]
    stp     x18, x19, [sp, #0x090]
    stp     x20, x21, [sp, #0x0A0]
    stp     x22, x23, [sp, #0x0B0]
    stp     x24, x25, [sp, #0x0C0]
    stp     x26, x27, [sp, #0x0D0]
    stp     x28, x29, [sp, #0x0E0]
    str     x30,       [sp, #0x0F0]

    mrs     x10, ELR_EL1
    str     x10,       [sp, #0x0F8]

    mrs     x10, SPSR_EL1
    str     x10,       [sp, #0x100]
.endm

.macro restore_el1_context
    ldr     x10,       [sp, #0x100]
    msr     SPSR_EL1, x10

    ldr     x10,       [sp, #0x0F8]
    msr     ELR_EL1, x10

    ldr     x30,       [sp, #0x0F0]
    ldp     x28, x29, [sp, #0x0E0]
    ldp     x26, x27, [sp, #0x0D0]
    ldp     x24, x25, [sp, #0x0C0]
    ldp     x22, x23, [sp, #0x0B0]
    ldp     x20, x21, [sp, #0x0A0]
    ldp     x18, x19, [sp, #0x090]
    ldp     x16, x17, [sp, #0x080]
    ldp     x14, x15, [sp, #0x070]
    ldp     x12, x13, [sp, #0x060]
    ldp     x10, x11, [sp, #0x050]
    ldp     x8,  x9,  [sp, #0x040]
    ldp     x6,  x7,  [sp, #0x030]
    ldp     x4,  x5,  [sp, #0x020]
    ldp     x2,  x3,  [sp, #0x010]
    ldp     x0,  x1,  [sp, #0x000]

    add     sp, sp, #0x110
.endm

.equ EC_SVC64,     0x15
.equ EC_IABT_EL0,  0x20
.equ EC_DABT_EL0,  0x24

_el0_sync_handler:
    save_el0_context

    mrs     x19, ESR_EL1
    lsr     x20, x19, #26

    cmp     x20, #EC_SVC64
    b.eq    _el0_svc_dispatch

    cmp     x20, #EC_DABT_EL0
    b.eq    _el0_dabt_dispatch

    cmp     x20, #EC_IABT_EL0
    b.eq    _el0_iabt_dispatch

    mov     x0, x19
    mrs     x1, ELR_EL1
    bl      arm_el0_unknown_exception
    restore_el0_context
    eret

_el0_svc_dispatch:

    ldr     x4, [sp, #0x000]
    ldr     x5, [sp, #0x008]
    ldr     x6, [sp, #0x010]
    ldr     x7, [sp, #0x018]

    sub     sp, sp, #80

    mov     x8, #0
    str     w8, [sp, #0]
    str     w8, [sp, #4]
    str     w8, [sp, #8]
    str     w8, [sp, #12]
    str     w8, [sp, #16]
    str     w8, [sp, #20]

    str     w5, [sp, #24]
    str     w7, [sp, #28]
    str     w6, [sp, #32]
    str     w4, [sp, #36]

    mov     w8, #128
    str     w8, [sp, #40]
    mov     w8, #0
    str     w8, [sp, #44]
    str     w8, [sp, #48]
    str     w8, [sp, #52]
    str     w8, [sp, #56]
    str     w8, [sp, #60]
    str     w8, [sp, #64]

    mov     x0, sp
    bl      k_syscall_entry

    ldr     w0, [sp, #36]

    add     sp, sp, #80

    str     x0, [sp, #0x000]

    bl      hvf_sync_pagetables

    restore_el0_context
    eret

_el0_dabt_dispatch:
    mrs     x0, FAR_EL1
    mov     x1, x19
    mrs     x2, ELR_EL1
    bl      arm_dabt_handler_c

    bl      hvf_sync_pagetables
    restore_el0_context
    eret

_el0_iabt_dispatch:
    mrs     x0, FAR_EL1
    mov     x1, x19
    mrs     x2, ELR_EL1
    bl      arm_dabt_handler_c

    bl      hvf_sync_pagetables
    restore_el0_context
    eret

_el0_irq_handler:
    save_el0_context

    bl      arm_irq_dispatch

    bl      hvf_sync_pagetables

    restore_el0_context
    eret

.equ EC_DABT_EL1,  0x25
.equ EC_IABT_EL1,  0x21

_el1_sync_handler:
    save_el1_context

    mrs     x19, ESR_EL1
    lsr     x20, x19, #26

    cmp     x20, #EC_DABT_EL1
    b.eq    _el1_dabt_dispatch

    cmp     x20, #EC_IABT_EL1
    b.eq    _el1_iabt_dispatch

    ldr     x0, =el1_sync_msg
    bl      arm_uart_puts

    mov     x0, #0
    mov     x1, x19
    mrs     x2, ELR_EL1
    bl      arm_dabt_handler_c
    b       .

_el1_dabt_dispatch:
    mrs     x0, FAR_EL1
    mov     x1, x19
    ldr     x2, [sp, #0x0F8]
    bl      arm_dabt_handler_c

    restore_el1_context
    eret

_el1_iabt_dispatch:
    mrs     x0, FAR_EL1
    mov     x1, x19
    ldr     x2, [sp, #0x0F8]
    bl      arm_dabt_handler_c

    restore_el1_context
    eret

_el1_irq_handler:
    save_el1_context

    bl      arm_irq_dispatch

    bl      hvf_sync_pagetables

    restore_el1_context
    eret

.section .rodata
el1_sync_msg:   .asciz "KERNEL SYNC EXCEPTION (EL1)\n"
