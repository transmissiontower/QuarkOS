#include "arch/aarch64/platform.h"
#include "kernel/core/klog.h"
#include "kernel/core/process.h"
#include "kernel/lib/util.h"

#define MAX_IRQ 128
static void (*irq_handlers[MAX_IRQ])(struct InterruptRegisters *r);

void arm_gic_init(void) {
    uint32_t dist = GIC_DIST_VIRT;
    uint32_t redist = GIC_REDIST_VIRT;
    uint32_t sgi = redist + GICR_SGI_BASE;

    mmio_write32(dist + GICD_CTLR, 0);
    mmio_write32(dist + GICD_CTLR, 1u << 5);

    uint32_t waker = mmio_read32(redist + GICR_WAKER);
    mmio_write32(redist + GICR_WAKER, waker & ~GICR_WAKER_PROCESSOR_SLEEP);
    while (mmio_read32(redist + GICR_WAKER) & GICR_WAKER_CHILDREN_ASLEEP)
        arm_dsb();

    for (int i = 0; i < 4; i++) {
        mmio_write32(dist + GICD_ICENABLER + i * 4, 0xFFFFFFFF);
        mmio_write32(dist + GICD_ICPENDR + i * 4, 0xFFFFFFFF);
        mmio_write32(dist + GICD_IPRIORITYR + i * 4, 0xA0A0A0A0);
    }

    for (int i = 1; i < 4; i++)
        mmio_write32(dist + GICD_IGROUPR + i * 4, 0xFFFFFFFF);

    for (uint32_t irq = 32; irq < MAX_IRQ; irq++) {
        uint32_t route = dist + GICD_IROUTER + irq * 8;
        mmio_write32(route, 0);
        mmio_write32(route + 4, 0);
    }

    for (int i = 0; i < 8; i++)
        mmio_write32(sgi + GICR_IPRIORITYR + i * 4, 0xA0A0A0A0);

    mmio_write32(sgi + GICR_IGROUPR0, 0xFFFFFFFF);
    mmio_write32(sgi + GICR_ICENABLER0, 0xFFFFFFFF);
    mmio_write32(sgi + GICR_ICPENDR0, 0xFFFFFFFF);

    uint64_t sre = 1;
    asm volatile("msr ICC_SRE_EL1, %0" ::"r"(sre) : "memory");
    arm_isb();
    uint64_t priority_mask = 0xFF;
    asm volatile("msr ICC_PMR_EL1, %0" ::"r"(priority_mask) : "memory");
    uint64_t binary_point = 0;
    asm volatile("msr ICC_BPR1_EL1, %0" ::"r"(binary_point) : "memory");
    uint64_t group1_enable = 1;
    asm volatile("msr ICC_IGRPEN1_EL1, %0" ::"r"(group1_enable) : "memory");
    arm_isb();

    mmio_write32(dist + GICD_CTLR, (1u << 5) | (1u << 1));

    for (int i = 0; i < MAX_IRQ; i++)
        irq_handlers[i] = 0;

    arm_uart_puts("  GICv3 distributor, redistributor, and CPU interface ready\n");
}

void arm_gic_enable_irq(uint32_t irq) {
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    if (irq < 32) {
        mmio_write32(GIC_REDIST_VIRT + GICR_SGI_BASE + GICR_ISENABLER0, 1u << bit);
        return;
    }
    if (irq < MAX_IRQ)
        mmio_write32(GIC_DIST_VIRT + GICD_ISENABLER + reg * 4, 1u << bit);
}

void arm_gic_disable_irq(uint32_t irq) {
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    if (irq < 32) {
        mmio_write32(GIC_REDIST_VIRT + GICR_SGI_BASE + GICR_ICENABLER0, 1u << bit);
        return;
    }
    if (irq < MAX_IRQ)
        mmio_write32(GIC_DIST_VIRT + GICD_ICENABLER + reg * 4, 1u << bit);
}

uint32_t arm_gic_ack(void) {
    uint64_t iar;
    asm volatile("mrs %0, ICC_IAR1_EL1" : "=r"(iar));
    return (uint32_t)iar & 0x3FF;
}

void arm_gic_eoi(uint32_t irq) {
    uint64_t iar = irq;
    asm volatile("msr ICC_EOIR1_EL1, %0" ::"r"(iar) : "memory");
}

void arm_gic_install_irq_handler(uint32_t irq, void (*handler)(struct InterruptRegisters *r)) {
    if (irq >= MAX_IRQ)
        return;
    irq_handlers[irq] = handler;
    arm_gic_enable_irq(irq);
}

void arm_irq_dispatch(void) {
    uint32_t irq = arm_gic_ack();
    if (irq >= 1020)
        return;

    if (irq == ARM_IRQ_TIMER) {
        static int timer_trace = 1;
        if (timer_trace) {
            timer_trace = 0;
            arm_uart_puts("HVF trace: physical timer IRQ\n");
        }
    }

    arm_gic_eoi(irq);

    if (irq < MAX_IRQ && irq_handlers[irq]) {
        struct InterruptRegisters fake;
        memset(&fake, 0, sizeof(fake));
        irq_handlers[irq](&fake);
    }

    extern int virtio_input_get_kbd_irq(void);
    extern int virtio_input_get_mouse_irq(void);
    extern void virtio_input_irq(uint32_t irq);
    extern void virtio_input_poll(void);
    int ki = virtio_input_get_kbd_irq();
    int mi = virtio_input_get_mouse_irq();
    if ((int)irq == ki || (int)irq == mi)
        virtio_input_irq(irq);

    if (irq == ARM_IRQ_TIMER || irq == ARM_IRQ_VTIMER) {
        virtio_input_poll();
    }
}

void arm_el0_unknown_exception(uint64_t esr, uint64_t elr) {
    extern process_t *volatile current_process;
    KLOG_PROC(LOG_CRIT, "UNKNOWN_EL0 pid=%d name='%s' ESR=%x ELR=%x EC=%x — terminating",
              current_process ? current_process->pid : -1,
              current_process ? current_process->name : "?", (uint32_t)esr, (uint32_t)elr,
              (uint32_t)(esr >> 26) & 0x3F);

    if (current_process && current_process->pid != 0) {
        extern void k_proc_terminate(int pid);
        k_proc_terminate(current_process->pid);
        arm_irq_enable();
        for (;;)
            arm_wfi();
    } else {
        extern void kernel_panic(const char *msg);
        kernel_panic("Unknown EL0 exception in kernel process");
    }
}

void arm_dabt_handler_c(uint64_t fault_addr, uint32_t fault_status, uint64_t fault_pc) {
    uint32_t dfsc = fault_status & 0x3F;
    uint32_t wnr = (fault_status >> 6) & 1;
    const char *fault_type = dfsc == 0x21   ? "ALIGNMENT"
                             : dfsc == 0x04 ? "XLAT-L0"
                             : dfsc == 0x05 ? "XLAT-L1"
                             : dfsc == 0x06 ? "XLAT-L2"
                             : dfsc == 0x07 ? "XLAT-L3"
                             : dfsc == 0x09 ? "ACCESS-L1"
                             : dfsc == 0x0B ? "ACCESS-L3"
                             : dfsc == 0x0D ? "PERM-L1"
                             : dfsc == 0x0F ? "PERM-L3"
                                            : "UNKNOWN";

    KLOG_GIC(LOG_CRIT, "===== %s ABORT =====", wnr ? "WRITE" : "READ");
    KLOG_GIC(LOG_CRIT, "FAR_EL1  = %lx (fault address)", (uint64_t)fault_addr);
    KLOG_GIC(LOG_CRIT, "ELR_EL1  = %lx (faulting PC)", (uint64_t)fault_pc);
    KLOG_GIC(LOG_CRIT, "ESR_EL1  = %x  EC=%x  DFSC=%x (%s)", fault_status, fault_status >> 26, dfsc,
             fault_type);

    {
        extern void debug_dump_pt_walk(uint64_t va);
        debug_dump_pt_walk((uint64_t)fault_addr);
    }

    uint64_t fp;
    asm volatile("mov %0, x29" : "=r"(fp));
    KLOG_GIC(LOG_CRIT, "--- Stack trace (FP chain) ---");
    for (int depth = 0; depth < 16 && fp != 0; depth++) {
        if (fp < 0xC0000000 || fp > 0xFFFFFFFF)
            break;
        uint64_t *frame = (uint64_t *)fp;
        uint64_t saved_lr = frame[1];
        uint64_t saved_fp = frame[0];
        KLOG_GIC(LOG_CRIT, "  #%d: LR=%lx FP=%lx", depth, saved_lr, saved_fp);
        fp = saved_fp;
    }

    uint64_t sp_val;
    asm volatile("mov %0, sp" : "=r"(sp_val));
    KLOG_GIC(LOG_CRIT, "SP=%lx", sp_val);

    extern process_t *volatile current_process;
    if (current_process) {
        KLOG_GIC(LOG_CRIT, "Process: pid=%d name='%s' pd=%x", current_process->pid,
                 current_process->name, (uint32_t)current_process->pd_phys);
    }

    {
        uintptr_t cp = (uintptr_t)current_process;
        int cp_valid = cp >= 0xD0000000UL && cp < 0xE0000000UL;
        if (cp_valid && current_process->pid > 0 && current_process->pid < 64) {
            KLOG_PROC(LOG_CRIT, "CRASH pid=%d name='%s' FAR=%x ELR=%x ESR=%x", current_process->pid,
                      current_process->name, (uint32_t)fault_addr, (uint32_t)fault_pc,
                      fault_status);
            extern void k_proc_terminate(int pid);
            k_proc_terminate(current_process->pid);
            arm_irq_enable();
            for (;;)
                arm_wfi();
        } else {
            extern void kernel_panic(const char *msg);
            kernel_panic("Data Abort (no valid process)");
        }
    }
}
