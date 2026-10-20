.section .boot, "ax"
.global _start
.global aarch64_kernel_l1
.global stack_top

.extern platform_init
.extern _bss_start
.extern _bss_end

#define PHYS_OFFSET     0x40000000
#define KERN_VIRT       0xC0000000
#define PERIPH_PHYS     0x08000000
#define PERIPH_VIRT     0xF4000000
#define VECTORS_VIRT    0xFFF00000
#define VECTORS_PHYS    0x5FF00000

#define DESC_TABLE      0x3
#define DESC_BLOCK      0x1

#define BLOCK_RAM       (DESC_BLOCK | (0 << 2) | (0 << 6) | (3 << 8) | (1 << 10))

#define BLOCK_DEV       (DESC_BLOCK | (1 << 2) | (0 << 6) | (0 << 8) | (1 << 10))

#define MAIR_VALUE      0x00000000000000FF

#define TCR_T0SZ        (25)
#define TCR_IRGN0_WBWA  (1 << 8)
#define TCR_ORGN0_WBWA  (1 << 10)
#define TCR_SH0_INNER   (3 << 12)
#define TCR_TG0_4K      (0 << 14)
#define TCR_VALUE        (TCR_T0SZ | TCR_IRGN0_WBWA | TCR_ORGN0_WBWA | \
                          TCR_SH0_INNER | TCR_TG0_4K)

_start:

    // U-Boot enters at EL2. Drop to EL1.
    mrs     x0, CurrentEL
    cmp     x0, #0x8
    b.ne    7f

    msr     daifset, #0xF
    mov     x0, #(1 << 31)       // HCR_EL2.RW: EL1 runs AArch64
    msr     hcr_el2, x0
    mov     x0, #3               // Permit EL1 physical and virtual timers
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr
    msr     cptr_el2, xzr
    ldr     x0, =7f
    ldr     x1, =(KERN_VIRT - PHYS_OFFSET)
    sub     x0, x0, x1           // in case MMU is disabled
    msr     elr_el2, x0
    mov     x0, #0x3C5           // EL1h, mask debug/SError/IRQ/FIQ
    msr     spsr_el2, x0
    dsb     sy
    mrs     x1, sctlr_el1
    bic     x1, x1, #(1 << 0)    // Disable inherited EL1 MMU
    bic     x1, x1, #(1 << 2)    // Disable inherited EL1 data cache
    bic     x1, x1, #(1 << 12)   // Disable inherited EL1 instruction cache
    msr     sctlr_el1, x1
    mrs     x1, sctlr_el2
    bic     x1, x1, #(1 << 0)    // Disable EL2 MMU for EL1 physical entry
    bic     x1, x1, #(1 << 2)    // Disable EL2 data cache
    bic     x1, x1, #(1 << 12)   // Disable EL2 instruction cache
    msr     sctlr_el2, x1
    isb
    eret

7:
    mrs     x0, CurrentEL
    cmp     x0, #0x4
    b.ne    8f
    dsb     sy
    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 0)
    bic     x0, x0, #(1 << 2)
    bic     x0, x0, #(1 << 12)
    msr     sctlr_el1, x0
    isb
8:

    msr     daifset, #0xF

    ldr     x4, =aarch64_kernel_l1
    ldr     x5, =(KERN_VIRT - PHYS_OFFSET)
    sub     x4, x4, x5

    ldr     x20, =l2_identity
    sub     x20, x20, x5

    ldr     x21, =l2_kernel
    sub     x21, x21, x5

    mov     x0, x4
    mov     x1, #(4096 * 3)
    bl      _zero_mem

    orr     x0, x20, #DESC_TABLE
    str     x0, [x4, #(1 * 8)]

    orr     x0, x21, #DESC_TABLE
    str     x0, [x4, #(3 * 8)]

    mov     x0, x20
    ldr     x1, =PHYS_OFFSET
    ldr     x2, =BLOCK_RAM
    orr     x1, x1, x2
    mov     x3, #256
1:
    str     x1, [x0], #8
    add     x1, x1, #(2 * 1024 * 1024)
    subs    x3, x3, #1
    b.ne    1b

    mov     x0, x21
    ldr     x1, =PHYS_OFFSET
    ldr     x2, =BLOCK_RAM
    orr     x1, x1, x2
    mov     x3, #256
2:
    str     x1, [x0], #8
    add     x1, x1, #(2 * 1024 * 1024)
    subs    x3, x3, #1
    b.ne    2b

    add     x0, x21, #(416 * 8)
    ldr     x1, =PERIPH_PHYS
    ldr     x2, =BLOCK_DEV
    orr     x1, x1, x2
    mov     x3, #24
3:
    str     x1, [x0], #8
    add     x1, x1, #(2 * 1024 * 1024)
    subs    x3, x3, #1
    b.ne    3b

    add     x0, x21, #(511 * 8)
    ldr     x1, =VECTORS_PHYS
    ldr     x2, =BLOCK_RAM
    orr     x1, x1, x2
    str     x1, [x0]

    ldr     x0, =MAIR_VALUE
    msr     mair_el1, x0

    ldr     x0, =TCR_VALUE
    msr     tcr_el1, x0

    msr     ttbr0_el1, x4

    tlbi    vmalle1
    dsb     sy
    isb

    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 19)
    msr     sctlr_el1, x0
    isb

    mrs     x0, sctlr_el1
    orr     x0, x0, #(1 << 0)
    orr     x0, x0, #(1 << 2)
    orr     x0, x0, #(1 << 12)
    bic     x0, x0, #(1 << 3)
    bic     x0, x0, #(1 << 4)
    bic     x0, x0, #(1 << 1)
    msr     sctlr_el1, x0
    isb

    ldr     x0, =_high_entry
    br      x0

_zero_mem:
    mov     x2, #0
.Lzero_loop:
    cbz     x1, .Lzero_done
    str     x2, [x0], #8
    sub     x1, x1, #8
    b       .Lzero_loop
.Lzero_done:
    ret

.section .text

_high_entry:

    mrs     x0, cpacr_el1
    orr     x0, x0, #(3 << 20)
    msr     cpacr_el1, x0
    isb

    ldr     x0, =stack_top
    mov     sp, x0

    ldr     x0, =_bss_start
    ldr     x1, =_bss_end
4:
    cmp     x0, x1
    b.ge    5f
    str     xzr, [x0], #8
    b       4b
5:

    bl      platform_init

6:
    wfi
    b       6b

.section .bss
.balign 16

irq_stack_bottom:
    .space  8192
    .balign 16
irq_stack_top:

svc_stack_bottom:
    .space  131072
    .balign 16
stack_top:

.section .data
.balign 4096

aarch64_kernel_l1:
    .space  4096

.balign 4096
l2_identity:
    .space  4096

.balign 4096
l2_kernel:
    .space  4096
