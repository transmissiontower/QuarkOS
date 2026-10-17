// SPDX-License-Identifier: MIT
.section .text
.global switch_context
.global enter_user_mode
.global irq_ret_stub
.extern hvf_sync_pagetables

switch_context:

    sub     sp, sp, #0x70
    stp     x19, x20, [sp, #0x00]
    stp     x21, x22, [sp, #0x10]
    stp     x23, x24, [sp, #0x20]
    stp     x25, x26, [sp, #0x30]
    stp     x27, x28, [sp, #0x40]
    stp     x29, x30, [sp, #0x50]

    mov     x9, sp
    str     x9, [x0]

    mov     sp, x1

    dsb     sy
    isb

    mrs     x10, ICC_PMR_EL1
    mrs     x10, ICC_PMR_EL1
    mrs     x10, ICC_PMR_EL1
    mrs     x10, ICC_PMR_EL1
    dsb     sy
    mrs     x10, ICC_PMR_EL1
    mrs     x10, ICC_PMR_EL1
    mrs     x10, ICC_PMR_EL1
    mrs     x10, ICC_PMR_EL1
    dsb     sy
    mrs     x10, ICC_PMR_EL1
    mrs     x10, ICC_PMR_EL1
    mrs     x10, ICC_PMR_EL1
    mrs     x10, ICC_PMR_EL1
    dsb     sy
    mrs     x10, ICC_PMR_EL1
    mrs     x10, ICC_PMR_EL1
    mrs     x10, ICC_PMR_EL1
    mrs     x10, ICC_PMR_EL1
    dsb     sy
    tlbi    vmalle1
    dsb     sy
    isb

    ldp     x19, x20, [sp, #0x00]
    ldp     x21, x22, [sp, #0x10]
    ldp     x23, x24, [sp, #0x20]
    ldp     x25, x26, [sp, #0x30]
    ldp     x27, x28, [sp, #0x40]
    ldp     x29, x30, [sp, #0x50]
    add     sp, sp, #0x70

    ret

irq_ret_stub:
    ldp     x0, x1, [sp], #16

enter_user_mode:

    msr     DAIFSet, #0xF

    msr     SP_EL0, x1

    msr     ELR_EL1, x0

    mov     x2, #0x0
    msr     SPSR_EL1, x2

    isb

    bl      hvf_sync_pagetables

    eret
