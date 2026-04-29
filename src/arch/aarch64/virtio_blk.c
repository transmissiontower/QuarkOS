// SPDX-License-Identifier: MIT
/*
 * src/arch/aarch64/virtio_blk.c - VirtIO block device driver for disk I/O
 */
#include "arch/aarch64/platform.h"
#include "kernel/klog.h"
#include "util/util.h"

#define VIRTIO_STATUS_ACKNOWLEDGE  1
#define VIRTIO_STATUS_DRIVER       2
#define VIRTIO_STATUS_DRIVER_OK    4
#define VIRTIO_STATUS_FEATURES_OK  8

#define VIRTIO_MMIO_QUEUE_PFN      0x040
#define VIRTIO_MMIO_QUEUE_ALIGN    0x03c
#define VIRTIO_MMIO_GUEST_PAGE_SZ  0x028

#define VIRTIO_BLK_F_RO       (1u << 5)
#define VIRTIO_BLK_F_BLK_SIZE (1u << 6)
#define VIRTIO_BLK_F_GEOMETRY (1u << 4)

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[];
} __attribute__((packed));

#define VIRTQ_DESC_F_NEXT   1
#define VIRTQ_DESC_F_WRITE  2

#define VIRTIO_BLK_T_IN    0
#define VIRTIO_BLK_T_OUT   1

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

#define VIRTIO_BLK_CFG_CAPACITY  0x100

#define BLK_QUEUE_SIZE  16

typedef struct {
    int        active;
    uint32_t   base;
    uint32_t   version;
    uint32_t   irq;
    uint64_t   capacity;
    int        read_only;
    disk_type_t type;

    struct virtq_desc     desc[BLK_QUEUE_SIZE]    __attribute__((aligned(4096)));
    uint8_t               avail_buf[4096]         __attribute__((aligned(4096)));
    uint8_t               used_buf[4096]          __attribute__((aligned(4096)));

    volatile struct virtq_avail *avail;
    volatile struct virtq_used  *used;
    uint16_t last_used;

    struct virtio_blk_req req_hdr   __attribute__((aligned(64)));
    uint8_t               req_status __attribute__((aligned(64)));
} blk_device_t;

static blk_device_t blk_devs[MAX_BLK_DEVS];
static int blk_dev_count = 0;

static inline uint32_t blk_va_to_pa(void *va)
{
    return (uint32_t)(uintptr_t)va - 0xC0000000 + 0x40000000;
}

static void blk_clean_range(void *addr, uint32_t size)
{
    uint64_t base = (uint64_t)(uintptr_t)addr;
    for (uint64_t off = 0; off < size; off += 64)
        asm volatile("dc cvac, %0" :: "r"(base + off) : "memory");
    arm_dsb();
}

static void blk_inval_range(void *addr, uint32_t size)
{
    uint64_t base = (uint64_t)(uintptr_t)addr;
    for (uint64_t off = 0; off < size; off += 64)
        asm volatile("dc ivac, %0" :: "r"(base + off) : "memory");
    arm_dsb();
}

static disk_type_t classify_disk(uint64_t capacity, int read_only)
{

    if (capacity <= 5760)
        return DISK_TYPE_FLOPPY;

    if (read_only)
        return DISK_TYPE_CDROM;

    return DISK_TYPE_HDD;
}

static const char *disk_type_name(disk_type_t t)
{
    switch (t) {
    case DISK_TYPE_HDD:     return "HDD";
    case DISK_TYPE_CDROM:   return "CD-ROM";
    case DISK_TYPE_FLOPPY:  return "Floppy";
    case DISK_TYPE_USB:     return "USB";
    default:                return "Unknown";
    }
}

static int blk_init_one(uint32_t slot)
{
    if (blk_dev_count >= MAX_BLK_DEVS)
        return -1;

    uint32_t base = VIRTIO_MMIO_VIRT(slot);
    uint32_t magic = mmio_read32(base + VIRTIO_MMIO_MAGIC_VALUE);
    if (magic != 0x74726976)
        return -1;

    uint32_t version = mmio_read32(base + VIRTIO_MMIO_VERSION);
    uint32_t dev_id  = mmio_read32(base + VIRTIO_MMIO_DEVICE_ID);

    if (dev_id != 2)
        return -1;
    if (version != 1 && version != 2)
        return -1;

    int idx = blk_dev_count;
    blk_device_t *dev = &blk_devs[idx];
    memset(dev, 0, sizeof(*dev));

    KLOG_BLK(LOG_INFO, "disk %d: found virtio-blk at slot %u, version %u",
             idx, slot, version);

    dev->base = base;
    dev->version = version;
    dev->irq = ARM_IRQ_VIRTIO_BASE + slot;

    mmio_write32(base + VIRTIO_MMIO_STATUS, 0);

    mmio_write32(base + VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    mmio_write32(base + VIRTIO_MMIO_STATUS,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    mmio_write32(base + VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    uint32_t dev_features = mmio_read32(base + VIRTIO_MMIO_DEVICE_FEATURES);

    dev->read_only = (dev_features & VIRTIO_BLK_F_RO) ? 1 : 0;

    mmio_write32(base + VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    mmio_write32(base + VIRTIO_MMIO_DRIVER_FEATURES, 0);

    if (version == 2) {
        mmio_write32(base + VIRTIO_MMIO_STATUS,
                     VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                     VIRTIO_STATUS_FEATURES_OK);
        uint32_t st = mmio_read32(base + VIRTIO_MMIO_STATUS);
        if (!(st & VIRTIO_STATUS_FEATURES_OK)) {
            KLOG_BLK(LOG_ERROR, "disk %d: FEATURES_OK not set", idx);
            return -1;
        }
    }

    uint32_t cap_lo = mmio_read32(base + VIRTIO_BLK_CFG_CAPACITY);
    uint32_t cap_hi = mmio_read32(base + VIRTIO_BLK_CFG_CAPACITY + 4);
    dev->capacity = ((uint64_t)cap_hi << 32) | cap_lo;

    dev->type = classify_disk(dev->capacity, dev->read_only);

    {
        uint32_t mb = (uint32_t)(dev->capacity / 2048);
        if (mb >= 1024)
            KLOG_BLK(LOG_INFO, "sd%d: %s, %u sectors (%u GB)%s",
                     idx, disk_type_name(dev->type),
                     (uint32_t)dev->capacity, mb / 1024,
                     dev->read_only ? " [read-only]" : "");
        else
            KLOG_BLK(LOG_INFO, "sd%d: %s, %u sectors (%u MB)%s",
                     idx, disk_type_name(dev->type),
                     (uint32_t)dev->capacity, mb,
                     dev->read_only ? " [read-only]" : "");
    }

    mmio_write32(base + VIRTIO_MMIO_QUEUE_SEL, 0);
    uint32_t max = mmio_read32(base + VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max == 0) {
        KLOG_BLK(LOG_ERROR, "disk %d: queue_num_max is 0", idx);
        return -1;
    }

    uint32_t num = BLK_QUEUE_SIZE;
    if (num > max) num = max;
    mmio_write32(base + VIRTIO_MMIO_QUEUE_NUM, num);

    for (uint32_t j = 0; j < BLK_QUEUE_SIZE; j++) {
        dev->desc[j].addr  = 0;
        dev->desc[j].len   = 0;
        dev->desc[j].flags = 0;
        dev->desc[j].next  = 0;
    }

    uint32_t desc_pa  = blk_va_to_pa((void *)dev->desc);
    uint32_t avail_pa = blk_va_to_pa((void *)dev->avail_buf);
    uint32_t used_pa  = blk_va_to_pa((void *)dev->used_buf);

    if (version == 2) {
        dev->avail = (volatile struct virtq_avail *)dev->avail_buf;
        dev->used  = (volatile struct virtq_used  *)dev->used_buf;
        dev->avail->flags = 0;
        dev->avail->idx   = 0;
        dev->used->flags  = 0;
        dev->used->idx    = 0;

        mmio_write32(base + VIRTIO_MMIO_QUEUE_DESC_LOW,    desc_pa);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DESC_HIGH,   0);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DRIVER_LOW,  avail_pa);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DRIVER_HIGH, 0);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DEVICE_LOW,  used_pa);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DEVICE_HIGH, 0);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_READY, 1);
    } else {

        dev->avail = (volatile struct virtq_avail *)
            ((uint8_t *)dev->desc + BLK_QUEUE_SIZE * sizeof(struct virtq_desc));
        dev->avail->flags = 0;
        dev->avail->idx   = 0;

        dev->used = (volatile struct virtq_used *)
            ((uint8_t *)dev->desc + 4096);
        dev->used->flags = 0;
        dev->used->idx   = 0;

        mmio_write32(base + VIRTIO_MMIO_GUEST_PAGE_SZ, 4096);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_ALIGN, 4096);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_PFN, desc_pa >> 12);
    }

    dev->last_used = 0;

    blk_clean_range((void *)dev->desc, sizeof(dev->desc));
    blk_clean_range((void *)dev->avail, 256);
    blk_clean_range((void *)dev->used, 512);

    uint32_t status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                      VIRTIO_STATUS_DRIVER_OK;
    if (version == 2)
        status |= VIRTIO_STATUS_FEATURES_OK;
    mmio_write32(base + VIRTIO_MMIO_STATUS, status);

    dev->active = 1;
    blk_dev_count++;
    KLOG_BLK(LOG_INFO, "disk %d: initialized successfully", idx);
    return 0;
}

void virtio_blk_init(void)
{
    blk_dev_count = 0;
    memset(blk_devs, 0, sizeof(blk_devs));

    for (int i = VIRTIO_MMIO_COUNT - 1; i >= 0; i--)
        blk_init_one((uint32_t)i);

    if (blk_dev_count == 0)
        KLOG_BLK(LOG_WARN, "no virtio-blk devices found");
    else
        KLOG_BLK(LOG_INFO, "found %d disk(s)", blk_dev_count);
}

int virtio_blk_transfer_disk(int idx, uint64_t sector, uint32_t count,
                             void *buf, int write)
{
    if (idx < 0 || idx >= blk_dev_count)
        return -1;

    blk_device_t *dev = &blk_devs[idx];

    if (!dev->active) {
        KLOG_BLK(LOG_ERROR, "disk %d: transfer: device not active", idx);
        return -1;
    }
    if (write && dev->read_only) {
        KLOG_BLK(LOG_ERROR, "disk %d: write to read-only device", idx);
        return -1;
    }
    if (sector + count > dev->capacity) {
        KLOG_BLK(LOG_ERROR, "disk %d: sector %u+%u exceeds capacity %u",
                 idx, (uint32_t)sector, count, (uint32_t)dev->capacity);
        return -1;
    }
    if (count == 0 || count > 128) {
        KLOG_BLK(LOG_ERROR, "disk %d: invalid count %u", idx, count);
        return -1;
    }

    uint32_t data_len = count * 512;

    dev->req_hdr.type     = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    dev->req_hdr.reserved = 0;
    dev->req_hdr.sector   = sector;
    dev->req_status = 0xFF;

    dev->desc[0].addr  = (uint64_t)blk_va_to_pa(&dev->req_hdr);
    dev->desc[0].len   = sizeof(struct virtio_blk_req);
    dev->desc[0].flags = VIRTQ_DESC_F_NEXT;
    dev->desc[0].next  = 1;

    dev->desc[1].addr  = (uint64_t)blk_va_to_pa(buf);
    dev->desc[1].len   = data_len;
    dev->desc[1].flags = VIRTQ_DESC_F_NEXT |
                         (write ? 0 : VIRTQ_DESC_F_WRITE);
    dev->desc[1].next  = 2;

    dev->desc[2].addr  = (uint64_t)blk_va_to_pa(&dev->req_status);
    dev->desc[2].len   = 1;
    dev->desc[2].flags = VIRTQ_DESC_F_WRITE;
    dev->desc[2].next  = 0;

    blk_clean_range(&dev->req_hdr, sizeof(dev->req_hdr));
    blk_clean_range(&dev->req_status, 64);
    if (write)
        blk_clean_range(buf, data_len);
    blk_clean_range((void *)dev->desc, sizeof(dev->desc));

    uint16_t avail_idx = dev->avail->idx;
    dev->avail->ring[avail_idx % BLK_QUEUE_SIZE] = 0;
    arm_dsb();
    dev->avail->idx = avail_idx + 1;
    arm_dsb();

    blk_clean_range((void *)dev->avail, 256);

    mmio_write32(dev->base + VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    int timeout = 1000000;
    while (dev->last_used == dev->used->idx) {

        blk_inval_range((void *)dev->used, 512);
        arm_dsb();
        if (--timeout <= 0) {
            KLOG_BLK(LOG_ERROR, "disk %d: transfer timeout: sector=%u count=%u %s",
                     idx, (uint32_t)sector, count, write ? "WRITE" : "READ");
            return -1;
        }
    }

    uint32_t isr = mmio_read32(dev->base + VIRTIO_MMIO_INTERRUPT_STATUS);
    if (isr)
        mmio_write32(dev->base + VIRTIO_MMIO_INTERRUPT_ACK, isr);

    dev->last_used++;

    if (!write)
        blk_inval_range(buf, data_len);
    blk_inval_range(&dev->req_status, 64);

    if (dev->req_status != 0) {
        KLOG_BLK(LOG_ERROR, "disk %d: transfer failed: status=%u sector=%u",
                 idx, dev->req_status, (uint32_t)sector);
        return -1;
    }

    return 0;
}

int virtio_blk_get_disk_count(void)
{
    return blk_dev_count;
}

int virtio_blk_get_disk_info(int idx, disk_info_t *out)
{
    if (idx < 0 || idx >= blk_dev_count || !out)
        return -1;

    blk_device_t *dev = &blk_devs[idx];
    out->type        = (uint32_t)dev->type;
    out->capacity_lo = (uint32_t)(dev->capacity & 0xFFFFFFFF);
    out->capacity_hi = (uint32_t)(dev->capacity >> 32);
    out->read_only   = (uint32_t)dev->read_only;
    out->sector_size = 512;
    return 0;
}

int virtio_blk_read_sectors(uint64_t sector, uint32_t count, void *buf)
{
    return virtio_blk_transfer_disk(0, sector, count, buf, 0);
}

int virtio_blk_write_sectors(uint64_t sector, uint32_t count, const void *buf)
{
    return virtio_blk_transfer_disk(0, sector, count, (void *)buf, 1);
}

int virtio_blk_is_active(void)
{
    return (blk_dev_count > 0 && blk_devs[0].active);
}

uint64_t virtio_blk_get_capacity(void)
{
    if (blk_dev_count > 0)
        return blk_devs[0].capacity;
    return 0;
}
