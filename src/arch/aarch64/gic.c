// SPDX-License-Identifier: MIT
/*
 * src/arch/aarch64/gic.c - ARM Generic Interrupt Controller (GIC) driver
 */
#include "arch/aarch64/platform.h"
#include "kernel/klog.h"
#include "kernel/process.h"
#include "util/util.h"

#define MAX_IRQ 128
static void (*irq_handlers[MAX_IRQ])(struct InterruptRegisters *r);

void arm_gic_init(void)
{
    uint32_t dist = GIC_DIST_VIRT;
    uint32_t cpu  = GIC_CPU_VIRT;

    mmio_write32(dist + GICD_CTLR, 0);

    for (int i = 0; i < 4; i++)
        mmio_write32(dist + GICD_ICENABLER + i * 4, 0xFFFFFFFF);

    for (int i = 0; i < 4; i++)
        mmio_write32(dist + GICD_ICPENDR + i * 4, 0xFFFFFFFF);

    for (int i = 0; i < 32; i++)
        mmio_write32(dist + GICD_IPRIORITYR + i * 4, 0xA0A0A0A0);

    for (int i = 8; i < 32; i++)
        mmio_write32(dist + GICD_ITARGETSR + i * 4, 0x01010101);

    for (int i = 0; i < 8; i++)
        mmio_write32(dist + GICD_ICFGR + i * 4, 0x00000000);

    mmio_write32(dist + GICD_CTLR, 1);

    mmio_write32(cpu + GICC_PMR, 0xFF);
    mmio_write32(cpu + GICC_CTLR, 1);

    for (int i = 0; i < MAX_IRQ; i++)
        irq_handlers[i] = 0;

    arm_uart_puts("  GIC: distributor + CPU interface ready\n");
}

void arm_gic_enable_irq(uint32_t irq)
{
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    mmio_write32(GIC_DIST_VIRT + GICD_ISENABLER + reg * 4, (1u << bit));
}

void arm_gic_disable_irq(uint32_t irq)
{
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    mmio_write32(GIC_DIST_VIRT + GICD_ICENABLER + reg * 4, (1u << bit));
}

uint32_t arm_gic_ack(void)
{
    return mmio_read32(GIC_CPU_VIRT + GICC_IAR) & 0x3FF;
}

void arm_gic_eoi(uint32_t irq)
{
    mmio_write32(GIC_CPU_VIRT + GICC_EOIR, irq);
}

static int legacy_to_arm_irq(int legacy_irq)
{
    switch (legacy_irq) {
    case 0:  return ARM_IRQ_TIMER;
    case 1:  return 49;
    case 12: return 50;
    default: return -1;
    }
}

void irq_install_handler(int irq, void (*handler)(struct InterruptRegisters *r))
{
    int arm_irq = legacy_to_arm_irq(irq);
    if (arm_irq < 0 || arm_irq >= MAX_IRQ) return;
    irq_handlers[arm_irq] = handler;
    arm_gic_enable_irq(arm_irq);

    if (irq == 0 && ARM_IRQ_VTIMER < MAX_IRQ) {
        irq_handlers[ARM_IRQ_VTIMER] = handler;
        arm_gic_enable_irq(ARM_IRQ_VTIMER);
    }
}

void irq_uninstall_handler(int irq)
{
    int arm_irq = legacy_to_arm_irq(irq);
    if (arm_irq < 0 || arm_irq >= MAX_IRQ) return;
    irq_handlers[arm_irq] = 0;
    arm_gic_disable_irq(arm_irq);
}

void arm_irq_dispatch(void)
{
    uint32_t irq = arm_gic_ack();
    if (irq >= 1020) return;

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
    extern void process_wake_on_irq(int irq);
    extern int virtio_input_kbd_has_data(void);
    extern int virtio_input_mouse_has_data(void);
    int ki = virtio_input_get_kbd_irq();
    int mi = virtio_input_get_mouse_irq();
    if ((int)irq == ki || (int)irq == mi)
        virtio_input_irq(irq);

    if (irq == ARM_IRQ_TIMER || irq == ARM_IRQ_VTIMER) {

        mmio_write32(UART0_VIRT + UART_DR, 0);
        virtio_input_poll();
        int have_kbd = virtio_input_kbd_has_data();
        int have_mouse = virtio_input_mouse_has_data();
        if (have_kbd || have_mouse) {

            process_wake_on_irq(1);
            process_wake_on_irq(12);
        }
    }

    int legacy_irq = -1;
    if (irq == ARM_IRQ_TIMER || irq == ARM_IRQ_VTIMER) legacy_irq = 0;
    else if ((int)irq == ki) legacy_irq = 1;
    else if ((int)irq == mi) legacy_irq = 12;
    if (legacy_irq >= 0)
        process_wake_on_irq(legacy_irq);
}

static void print_hex(uint32_t val)
{
    for (int s = 28; s >= 0; s -= 4) {
        int n = (val >> s) & 0xF;
        arm_uart_putc(n < 10 ? '0'+n : 'A'+n-10);
    }
}

void arm_el0_unknown_exception(uint64_t esr, uint64_t elr)
{
    extern process_t * volatile current_process;
    KLOG_PROC(LOG_CRIT, "UNKNOWN_EL0 pid=%d name='%s' ESR=%x ELR=%x EC=%x — terminating",
              current_process ? current_process->pid : -1,
              current_process ? current_process->name : "?",
              (uint32_t)esr, (uint32_t)elr, (uint32_t)(esr >> 26) & 0x3F);

    if (current_process && current_process->pid != 0) {
        extern void k_proc_terminate(int pid);
        k_proc_terminate(current_process->pid);
        arm_irq_enable();
        for (;;) arm_wfi();
    } else {
        extern void kernel_panic(const char *msg);
        kernel_panic("Unknown EL0 exception in kernel process");
    }
}

void arm_dabt_handler_c(uint64_t fault_addr, uint32_t fault_status, uint64_t fault_pc)
{
    uint32_t dfsc = fault_status & 0x3F;
    uint32_t wnr  = (fault_status >> 6) & 1;
    const char *fault_type =
        dfsc == 0x21 ? "ALIGNMENT" :
        dfsc == 0x04 ? "XLAT-L0" :
        dfsc == 0x05 ? "XLAT-L1" :
        dfsc == 0x06 ? "XLAT-L2" :
        dfsc == 0x07 ? "XLAT-L3" :
        dfsc == 0x09 ? "ACCESS-L1" :
        dfsc == 0x0B ? "ACCESS-L3" :
        dfsc == 0x0D ? "PERM-L1" :
        dfsc == 0x0F ? "PERM-L3" : "UNKNOWN";

    KLOG_GIC(LOG_CRIT, "===== %s ABORT =====", wnr ? "WRITE" : "READ");
    KLOG_GIC(LOG_CRIT, "FAR_EL1  = %lx (fault address)", (uint64_t)fault_addr);
    KLOG_GIC(LOG_CRIT, "ELR_EL1  = %lx (faulting PC)", (uint64_t)fault_pc);
    KLOG_GIC(LOG_CRIT, "ESR_EL1  = %x  EC=%x  DFSC=%x (%s)",
             fault_status, fault_status >> 26, dfsc, fault_type);

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

    extern process_t * volatile current_process;
    if (current_process) {
        KLOG_GIC(LOG_CRIT, "Process: pid=%d name='%s' pd=%x",
                 current_process->pid, current_process->name,
                 (uint32_t)current_process->pd_phys);
    }

    {
        uintptr_t cp = (uintptr_t)current_process;
        int cp_valid = cp >= 0xD0000000UL && cp < 0xE0000000UL;
        if (cp_valid && current_process->pid > 0 && current_process->pid < 64) {
            KLOG_PROC(LOG_CRIT, "CRASH pid=%d name='%s' FAR=%x ELR=%x ESR=%x",
                      current_process->pid, current_process->name,
                      (uint32_t)fault_addr, (uint32_t)fault_pc, fault_status);
            extern void k_proc_terminate(int pid);
            k_proc_terminate(current_process->pid);
            arm_irq_enable();
            for (;;) arm_wfi();
        } else {
            extern void kernel_panic(const char *msg);
            kernel_panic("Data Abort (no valid process)");
        }
    }
}
