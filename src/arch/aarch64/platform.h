// SPDX-License-Identifier: MIT
#ifndef AARCH64_PLATFORM_H
#define AARCH64_PLATFORM_H

#include "lib/stdint.h"

struct InterruptRegisters;

#define ARM_PHYS_OFFSET 0x40000000
#define ARM_RAM_SIZE 0x20000000

#define ARM_UART0_BASE 0x09000000
#define ARM_GIC_DIST_BASE 0x08000000
#define ARM_GIC_REDIST_BASE 0x080A0000

#define VIRTIO_MMIO_BASE 0x0a000000
#define VIRTIO_MMIO_STRIDE 0x200
#define VIRTIO_MMIO_COUNT 32

#define VIRTIO_MMIO_MAGIC_VALUE 0x000
#define VIRTIO_MMIO_VERSION 0x004
#define VIRTIO_MMIO_DEVICE_ID 0x008
#define VIRTIO_MMIO_VENDOR_ID 0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL 0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM 0x038
#define VIRTIO_MMIO_QUEUE_READY 0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064
#define VIRTIO_MMIO_STATUS 0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW 0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW 0x090
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW 0x0a0
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH 0x0a4

#define ARM_PERIPH_VIRT 0xF4000000

#define ARM_MB_VIRT(phys) ((phys) - 0x08000000 + ARM_PERIPH_VIRT)
#define ARM_GIC_VIRT_ADDR(phys) ARM_MB_VIRT(phys)

#define UART0_VIRT ARM_MB_VIRT(ARM_UART0_BASE)
#define GIC_DIST_VIRT ARM_MB_VIRT(ARM_GIC_DIST_BASE)
#define GIC_REDIST_VIRT ARM_MB_VIRT(ARM_GIC_REDIST_BASE)

#define VIRTIO_MMIO_VIRT(n) ARM_MB_VIRT(VIRTIO_MMIO_BASE + (n) * VIRTIO_MMIO_STRIDE)

#define KERNEL_START 0xC0000000
#define MODULE_VIRT_START 0xD2000000

static inline void mmio_write32(uint32_t addr, uint32_t val) {
    asm volatile("str %w0, [%1]" ::"r"(val), "r"((uintptr_t)addr) : "memory");
}
static inline uint32_t mmio_read32(uint32_t addr) {
    uint32_t val;
    asm volatile("ldr %w0, [%1]" : "=r"(val) : "r"((uintptr_t)addr) : "memory");
    return val;
}
static inline void mmio_write16(uint32_t addr, uint16_t val) {
    asm volatile("strh %w0, [%1]" ::"r"((uint32_t)val), "r"((uintptr_t)addr) : "memory");
}
static inline uint16_t mmio_read16(uint32_t addr) {
    uint32_t val;
    asm volatile("ldrh %w0, [%1]" : "=r"(val) : "r"((uintptr_t)addr) : "memory");
    return (uint16_t)val;
}
static inline void mmio_write8(uint32_t addr, uint8_t val) {
    asm volatile("strb %w0, [%1]" ::"r"((uint32_t)val), "r"((uintptr_t)addr) : "memory");
}
static inline uint8_t mmio_read8(uint32_t addr) {
    uint32_t val;
    asm volatile("ldrb %w0, [%1]" : "=r"(val) : "r"((uintptr_t)addr) : "memory");
    return (uint8_t)val;
}

static inline void arm_irq_disable(void) {
    asm volatile("msr daifset, #2" ::: "memory");
}
static inline void arm_irq_enable(void) {
    asm volatile("msr daifclr, #2" ::: "memory");
}

static inline uint64_t arm_irq_save(void) {
    uint64_t flags;
    asm volatile("mrs %0, daif" : "=r"(flags)::"memory");
    asm volatile("msr daifset, #2" ::: "memory");
    return flags;
}

static inline void arm_irq_restore(uint64_t flags) {
    asm volatile("msr daif, %0" ::"r"(flags) : "memory");
}

static inline void arm_wfi(void) {
    asm volatile("wfi");
}
static inline void arm_dsb(void) {
    asm volatile("dsb sy" ::: "memory");
}
static inline void arm_dmb(void) {
    asm volatile("dmb sy" ::: "memory");
}
static inline void arm_isb(void) {
    asm volatile("isb" ::: "memory");
}

static inline void arm_set_ttbr0(uint32_t phys) {
    uint64_t val = phys;
    asm volatile("msr ttbr0_el1, %0" ::"r"(val) : "memory");
    arm_isb();
}
static inline uint32_t arm_get_ttbr0(void) {
    uint64_t val;
    asm volatile("mrs %0, ttbr0_el1" : "=r"(val));
    return (uint32_t)val;
}

static inline void arm_tlb_flush_all(void) {
    asm volatile("tlbi vmalle1" ::: "memory");
    arm_dsb();
    arm_isb();
}
static inline void arm_tlb_flush_single(uint32_t va) {
    uint64_t operand = (uint64_t)(va >> 12);
    asm volatile("tlbi vale1, %0" ::"r"(operand) : "memory");
    arm_dsb();
    arm_isb();
}

#define PAGE_DESC_PAGE (1UL << 1)

#define PAGE_FLAG_AF (1UL << 10)

#define PAGE_ATTR_NORMAL (0UL << 2)
#define PAGE_ATTR_DEVICE (1UL << 2)

#define PAGE_TABLE_DESC (0x3UL)

#define PAGE_BLOCK_DESC (0x1UL)

#define UART_DR 0x000
#define UART_FR 0x018
#define UART_FR_TXFF (1 << 5)

#define GICD_CTLR 0x000
#define GICD_IGROUPR 0x080
#define GICD_ISENABLER 0x100
#define GICD_ICENABLER 0x180
#define GICD_IPRIORITYR 0x400
#define GICD_ICPENDR 0x280
#define GICD_IROUTER 0x6100

#define GICR_WAKER 0x014
#define GICR_WAKER_PROCESSOR_SLEEP (1u << 1)
#define GICR_WAKER_CHILDREN_ASLEEP (1u << 2)
#define GICR_SGI_BASE 0x10000
#define GICR_IGROUPR0 0x080
#define GICR_ISENABLER0 0x100
#define GICR_ICENABLER0 0x180
#define GICR_ICPENDR0 0x280
#define GICR_IPRIORITYR 0x400

#define ARM_IRQ_TIMER 30
#define ARM_IRQ_VTIMER 27

#define ARM_IRQ_VIRTIO_BASE 48

#define ARM_FB_WIDTH 1920
#define ARM_FB_HEIGHT 1080
#define ARM_FB_BPP 32

extern void arm_uart_putc(char c);
extern void arm_uart_puts(const char *s);

void arm_gic_init(void);
void arm_gic_enable_irq(uint32_t irq);
void arm_gic_disable_irq(uint32_t irq);
void arm_gic_install_irq_handler(uint32_t irq, void (*handler)(struct InterruptRegisters *r));
uint32_t arm_gic_ack(void);
void arm_gic_eoi(uint32_t irq);
void arm_timer_init(void);
void arm_psci_system_off(void);
void arm_psci_system_reset(void);
void virtio_gpu_init(uint32_t fb_phys);
void virtio_gpu_flush(void);
void virtio_input_init(void);
void virtio_input_poll(void);
void virtio_input_irq(uint32_t irq);

int virtio_input_kbd_has_data(void);
uint8_t virtio_input_kbd_read(void);
int virtio_input_mouse_has_data(void);
uint8_t virtio_input_mouse_read(void);

typedef enum {
    DISK_TYPE_UNKNOWN = 0,
    DISK_TYPE_HDD = 1,
    DISK_TYPE_CDROM = 2,
    DISK_TYPE_FLOPPY = 3,
    DISK_TYPE_USB = 4,
} disk_type_t;

typedef struct {
    uint32_t type;
    uint32_t capacity_lo;
    uint32_t capacity_hi;
    uint32_t read_only;
    uint32_t sector_size;
} disk_info_t;

typedef struct {
    uint32_t sector;
    uint32_t count;
    void *buf;
} blk_io_req_t;

#define MAX_BLK_DEVS 8

void virtio_blk_init(void);
int virtio_blk_get_disk_count(void);
int virtio_blk_get_disk_info(int idx, disk_info_t *out);
int virtio_blk_transfer_disk(int idx, uint64_t sector, uint32_t count, void *buf, int write);

int virtio_blk_read_sectors(uint64_t sector, uint32_t count, void *buf);
int virtio_blk_write_sectors(uint64_t sector, uint32_t count, const void *buf);
int virtio_blk_is_active(void);
uint64_t virtio_blk_get_capacity(void);

void mm_vmm_unmap_page(uint32_t virt);

#endif
