// SPDX-License-Identifier: MIT
/*
 * src/arch/aarch64/virtio_gpu.c - VirtIO GPU driver for framebuffer display output
 */
#include "arch/aarch64/platform.h"
#include "util/util.h"

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8

#define VIRTIO_MMIO_QUEUE_PFN 0x040
#define VIRTIO_MMIO_QUEUE_ALIGN 0x03c
#define VIRTIO_MMIO_GUEST_PAGE_SZ 0x028

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

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

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF 0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT 0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING 0x0107

#define VIRTIO_GPU_RESP_OK_NODATA 0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1101

#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM 2
#define VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM 4

struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_rect {
    uint32_t x, y, width, height;
} __attribute__((packed));

struct virtio_gpu_display_one {
    struct virtio_gpu_rect r;
    uint32_t enabled;
    uint32_t flags;
} __attribute__((packed));

struct virtio_gpu_resp_display_info {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_display_one pmodes[16];
} __attribute__((packed));

struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed));

struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_attach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;

} __attribute__((packed));

struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_flush {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

#define QUEUE_SIZE 16

static struct virtq_desc vq_desc[512] __attribute__((aligned(4096)));
static uint8_t vq_avail_buf[4096] __attribute__((aligned(4096)));
static uint8_t vq_used_buf[4096] __attribute__((aligned(4096)));

static uint8_t cmd_buf[4096] __attribute__((aligned(4096)));
static uint8_t rsp_buf[4096] __attribute__((aligned(4096)));

static volatile struct virtq_avail *vq_avail;
static volatile struct virtq_used *vq_used;
static uint16_t vq_free_head;
static uint16_t vq_last_used;

static uint32_t gpu_base;
static uint32_t gpu_version;

static uint32_t saved_fb_phys;

static inline uint32_t va_to_pa(void *va) {
    return (uint32_t)va - 0xC0000000 + 0x40000000;
}

static void vq_init(void) {
    for (int i = 0; i < QUEUE_SIZE; i++) {
        vq_desc[i].next = (uint16_t)(i + 1);
        vq_desc[i].flags = 0;
    }
    vq_avail = (volatile struct virtq_avail *)vq_avail_buf;
    vq_used = (volatile struct virtq_used *)vq_used_buf;
    vq_avail->flags = 0;
    vq_avail->idx = 0;
    vq_used->flags = 0;
    vq_used->idx = 0;
    vq_free_head = 0;
    vq_last_used = 0;
}

static uint16_t alloc_desc(void) {
    uint16_t idx = vq_free_head;
    vq_free_head = vq_desc[idx].next;
    return idx;
}

static void free_desc(uint16_t idx) {
    vq_desc[idx].next = vq_free_head;
    vq_desc[idx].flags = 0;
    vq_free_head = idx;
}

static volatile int gpu_cmd_lock = 0;
static int gpu_init_done = 0;

static void gpu_poll_completion(void) {
    for (volatile int j = 0; j < 10000000; j++) {
        asm volatile("dc ivac, %0" ::"r"((uint64_t)(uintptr_t)&vq_used->idx) : "memory");
        arm_dmb();
        if (vq_used->idx != vq_last_used) {
            vq_last_used++;
            return;
        }
    }
    for (int i = 0; i < 60; i++) {
        uint32_t isr = mmio_read32(gpu_base + VIRTIO_MMIO_INTERRUPT_STATUS);
        if (isr)
            mmio_write32(gpu_base + VIRTIO_MMIO_INTERRUPT_ACK, isr);
        for (volatile int j = 0; j < 1000000; j++) {
            asm volatile("dc ivac, %0" ::"r"((uint64_t)(uintptr_t)&vq_used->idx) : "memory");
            arm_dmb();
            if (vq_used->idx != vq_last_used) {
                vq_last_used++;
                return;
            }
        }
    }
    arm_uart_puts("virtio-gpu: cmd timeout\n");
}

static void virtio_gpu_send_cmd(void *req, uint32_t req_len, void *rsp, uint32_t rsp_len) {
    uint32_t saved = arm_irq_save();

    if (__atomic_exchange_n(&gpu_cmd_lock, 1, __ATOMIC_ACQUIRE)) {
        arm_irq_restore(saved);
        return;
    }

    uint16_t d0 = alloc_desc();
    uint16_t d1 = alloc_desc();

    vq_desc[d0].addr = (uint64_t)va_to_pa(req);
    vq_desc[d0].len = req_len;
    vq_desc[d0].flags = VIRTQ_DESC_F_NEXT;
    vq_desc[d0].next = d1;

    vq_desc[d1].addr = (uint64_t)va_to_pa(rsp);
    vq_desc[d1].len = rsp_len;
    vq_desc[d1].flags = VIRTQ_DESC_F_WRITE;
    vq_desc[d1].next = 0;

    for (uint32_t off = 0; off < req_len; off += 64)
        asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)req + off) : "memory");

    if (!vq_avail || !vq_used) {
        arm_uart_puts("virtio-gpu: BUG vq_avail/used NULL\n");
        free_desc(d0);
        free_desc(d1);
        return;
    }
    uint16_t avail_idx = vq_avail->idx;
    vq_avail->ring[avail_idx % QUEUE_SIZE] = d0;

    arm_dsb();

    vq_avail->idx = avail_idx + 1;

    asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&vq_avail->idx) : "memory");
    asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&vq_avail->ring[avail_idx % QUEUE_SIZE])
                 : "memory");
    asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&vq_desc[d0]) : "memory");
    asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&vq_desc[d1]) : "memory");

    arm_dsb();

    mmio_write32(gpu_base + VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    if (!gpu_init_done) {
        gpu_poll_completion();
    } else {
        gpu_poll_completion();
    }

    free_desc(d0);
    free_desc(d1);

    __atomic_store_n(&gpu_cmd_lock, 0, __ATOMIC_RELEASE);
    arm_irq_restore(saved);
}

static void virtio_gpu_send_cmd3(void *hdr, uint32_t hdr_len, void *data, uint32_t data_len,
                                 void *rsp, uint32_t rsp_len) {
    uint32_t saved = arm_irq_save();
    if (__atomic_exchange_n(&gpu_cmd_lock, 1, __ATOMIC_ACQUIRE)) {
        arm_irq_restore(saved);
        return;
    }

    uint16_t d0 = alloc_desc();
    uint16_t d1 = alloc_desc();
    uint16_t d2 = alloc_desc();

    vq_desc[d0].addr = (uint64_t)va_to_pa(hdr);
    vq_desc[d0].len = hdr_len;
    vq_desc[d0].flags = VIRTQ_DESC_F_NEXT;
    vq_desc[d0].next = d1;

    vq_desc[d1].addr = (uint64_t)va_to_pa(data);
    vq_desc[d1].len = data_len;
    vq_desc[d1].flags = VIRTQ_DESC_F_NEXT;
    vq_desc[d1].next = d2;

    vq_desc[d2].addr = (uint64_t)va_to_pa(rsp);
    vq_desc[d2].len = rsp_len;
    vq_desc[d2].flags = VIRTQ_DESC_F_WRITE;
    vq_desc[d2].next = 0;

    for (uint32_t off = 0; off < hdr_len; off += 64)
        asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)hdr + off) : "memory");
    for (uint32_t off = 0; off < data_len; off += 64)
        asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)data + off) : "memory");

    if (!vq_avail || !vq_used) {
        arm_uart_puts("virtio-gpu: BUG vq_avail/used NULL (cmd3)\n");
        free_desc(d0);
        free_desc(d1);
        free_desc(d2);
        return;
    }

    uint16_t avail_idx = vq_avail->idx;
    vq_avail->ring[avail_idx % QUEUE_SIZE] = d0;
    arm_dsb();
    vq_avail->idx = avail_idx + 1;
    asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&vq_avail->idx) : "memory");
    asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&vq_avail->ring[avail_idx % QUEUE_SIZE])
                 : "memory");
    asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&vq_desc[d0]) : "memory");
    asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&vq_desc[d1]) : "memory");
    asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&vq_desc[d2]) : "memory");
    arm_dsb();

    mmio_write32(gpu_base + VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    if (!gpu_init_done) {
        gpu_poll_completion();
    } else {
        for (volatile int j = 0; j < 100000; j++) {
            asm volatile("dc ivac, %0" ::"r"((uint64_t)(uintptr_t)&vq_used->idx) : "memory");
            arm_dmb();
            if (vq_used->idx != vq_last_used) {
                vq_last_used++;
                break;
            }
        }
    }

    free_desc(d0);
    free_desc(d1);
    free_desc(d2);

    __atomic_store_n(&gpu_cmd_lock, 0, __ATOMIC_RELEASE);
    arm_irq_restore(saved);
}

static int virtio_dev_init(uint32_t base, uint32_t version) {
    mmio_write32(base + VIRTIO_MMIO_STATUS, 0);

    mmio_write32(base + VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);

    mmio_write32(base + VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    mmio_write32(base + VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    (void)mmio_read32(base + VIRTIO_MMIO_DEVICE_FEATURES);
    mmio_write32(base + VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    mmio_write32(base + VIRTIO_MMIO_DRIVER_FEATURES, 0);

    if (version == 2) {
        mmio_write32(base + VIRTIO_MMIO_STATUS,
                     VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);

        uint32_t st = mmio_read32(base + VIRTIO_MMIO_STATUS);
        if (!(st & VIRTIO_STATUS_FEATURES_OK))
            return -1;
    }

    mmio_write32(base + VIRTIO_MMIO_QUEUE_SEL, 0);
    uint32_t max = mmio_read32(base + VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max == 0)
        return -1;
    uint32_t num = QUEUE_SIZE;
    if (num > max)
        num = max;

    mmio_write32(base + VIRTIO_MMIO_QUEUE_NUM, num);

    vq_init();

    uint32_t desc_pa = va_to_pa((void *)vq_desc);
    uint32_t avail_pa = va_to_pa((void *)vq_avail_buf);
    uint32_t used_pa = va_to_pa((void *)vq_used_buf);

    if (version == 2) {
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DESC_LOW, desc_pa);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DESC_HIGH, 0);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DRIVER_LOW, avail_pa);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DRIVER_HIGH, 0);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DEVICE_LOW, used_pa);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DEVICE_HIGH, 0);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_READY, 1);
    } else {
        mmio_write32(base + VIRTIO_MMIO_GUEST_PAGE_SZ, 4096);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_ALIGN, 4096);

        vq_avail = (volatile struct virtq_avail *)((uint8_t *)vq_desc +
                                                   QUEUE_SIZE * sizeof(struct virtq_desc));
        vq_avail->flags = 0;
        vq_avail->idx = 0;

        uint32_t used_offset = 4096;
        vq_used = (volatile struct virtq_used *)((uint8_t *)vq_desc + used_offset);
        vq_used->flags = 0;
        vq_used->idx = 0;

        mmio_write32(base + VIRTIO_MMIO_QUEUE_PFN, desc_pa >> 12);
    }

    uint32_t status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK;
    if (version == 2)
        status |= VIRTIO_STATUS_FEATURES_OK;
    mmio_write32(base + VIRTIO_MMIO_STATUS, status);

    return 0;
}

static void gpu_get_display_info(void) {
    struct virtio_gpu_ctrl_hdr *req = (struct virtio_gpu_ctrl_hdr *)cmd_buf;
    memset(req, 0, sizeof(*req));
    req->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

    struct virtio_gpu_resp_display_info *rsp = (struct virtio_gpu_resp_display_info *)rsp_buf;
    memset(rsp, 0, sizeof(*rsp));

    virtio_gpu_send_cmd(req, sizeof(*req), rsp, sizeof(*rsp));
}

static void gpu_resource_create_2d(void) {
    struct virtio_gpu_resource_create_2d *req = (struct virtio_gpu_resource_create_2d *)cmd_buf;
    memset(req, 0, sizeof(*req));
    req->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    req->resource_id = 1;
    req->format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
    req->width = ARM_FB_WIDTH;
    req->height = ARM_FB_HEIGHT;

    struct virtio_gpu_ctrl_hdr *rsp = (struct virtio_gpu_ctrl_hdr *)rsp_buf;
    memset(rsp, 0, sizeof(*rsp));

    virtio_gpu_send_cmd(req, sizeof(*req), rsp, sizeof(*rsp));
}

static void gpu_resource_attach_backing(uint32_t fb_phys) {
    memset(cmd_buf, 0, sizeof(cmd_buf));
    struct virtio_gpu_resource_attach_backing *req =
        (struct virtio_gpu_resource_attach_backing *)cmd_buf;
    req->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    req->resource_id = 1;
    req->nr_entries = 1;

    struct virtio_gpu_mem_entry *entry = (struct virtio_gpu_mem_entry *)(cmd_buf + sizeof(*req));
    entry->addr = (uint64_t)fb_phys;
    entry->length = ARM_FB_WIDTH * ARM_FB_HEIGHT * 4;
    entry->padding = 0;

    struct virtio_gpu_ctrl_hdr *rsp = (struct virtio_gpu_ctrl_hdr *)rsp_buf;
    memset(rsp, 0, sizeof(*rsp));

    virtio_gpu_send_cmd(req, sizeof(*req) + sizeof(*entry), rsp, sizeof(*rsp));
}

static void gpu_set_scanout(void) {
    struct virtio_gpu_set_scanout *req = (struct virtio_gpu_set_scanout *)cmd_buf;
    memset(req, 0, sizeof(*req));
    req->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    req->r.x = 0;
    req->r.y = 0;
    req->r.width = ARM_FB_WIDTH;
    req->r.height = ARM_FB_HEIGHT;
    req->scanout_id = 0;
    req->resource_id = 1;

    struct virtio_gpu_ctrl_hdr *rsp = (struct virtio_gpu_ctrl_hdr *)rsp_buf;
    memset(rsp, 0, sizeof(*rsp));

    virtio_gpu_send_cmd(req, sizeof(*req), rsp, sizeof(*rsp));
}

static void gpu_transfer_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    struct virtio_gpu_transfer_to_host_2d *req = (struct virtio_gpu_transfer_to_host_2d *)cmd_buf;
    memset(req, 0, sizeof(*req));
    req->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    req->r.x = x;
    req->r.y = y;
    req->r.width = w;
    req->r.height = h;
    req->offset = (y * ARM_FB_WIDTH + x) * 4;
    req->resource_id = 1;

    struct virtio_gpu_ctrl_hdr *rsp = (struct virtio_gpu_ctrl_hdr *)rsp_buf;
    memset(rsp, 0, sizeof(*rsp));

    virtio_gpu_send_cmd(req, sizeof(*req), rsp, sizeof(*rsp));
}

static void gpu_flush_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    struct virtio_gpu_resource_flush *req = (struct virtio_gpu_resource_flush *)cmd_buf;
    memset(req, 0, sizeof(*req));
    req->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    req->r.x = x;
    req->r.y = y;
    req->r.width = w;
    req->r.height = h;
    req->resource_id = 1;

    struct virtio_gpu_ctrl_hdr *rsp = (struct virtio_gpu_ctrl_hdr *)rsp_buf;
    memset(rsp, 0, sizeof(*rsp));

    virtio_gpu_send_cmd(req, sizeof(*req), rsp, sizeof(*rsp));
}

static void gpu_transfer_to_host_2d(void) {
    gpu_transfer_rect(0, 0, ARM_FB_WIDTH, ARM_FB_HEIGHT);
}

static void gpu_resource_flush(void) {
    gpu_flush_rect(0, 0, ARM_FB_WIDTH, ARM_FB_HEIGHT);
}

void virtio_gpu_init(uint32_t fb_phys) {
    saved_fb_phys = fb_phys;
    gpu_base = 0;

    for (uint32_t i = 0; i < VIRTIO_MMIO_COUNT; i++) {
        uint32_t base = VIRTIO_MMIO_VIRT(i);
        uint32_t magic = mmio_read32(base + VIRTIO_MMIO_MAGIC_VALUE);
        if (magic != 0x74726976)
            continue;

        uint32_t version = mmio_read32(base + VIRTIO_MMIO_VERSION);
        uint32_t dev_id = mmio_read32(base + VIRTIO_MMIO_DEVICE_ID);

        if (dev_id != 16)
            continue;

        if (version != 1 && version != 2)
            continue;

        gpu_base = base;
        gpu_version = version;
        break;
    }

    if (gpu_base == 0) {
        arm_uart_puts("virtio-gpu: no device found\n");
        return;
    }

    arm_uart_puts("virtio-gpu: found device, version=");
    arm_uart_putc('0' + gpu_version);
    arm_uart_putc('\n');

    if (virtio_dev_init(gpu_base, gpu_version) < 0) {
        arm_uart_puts("virtio-gpu: device init failed\n");
        return;
    }

    arm_uart_puts("virtio-gpu: device initialised\n");

    gpu_get_display_info();
    arm_uart_puts("virtio-gpu: display info ok\n");

    gpu_resource_create_2d();
    arm_uart_puts("virtio-gpu: resource created\n");

    gpu_resource_attach_backing(fb_phys);
    arm_uart_puts("virtio-gpu: backing attached\n");

    gpu_set_scanout();
    arm_uart_puts("virtio-gpu: scanout set\n");

    gpu_transfer_to_host_2d();
    gpu_resource_flush();
    arm_uart_puts("virtio-gpu: initial flush done\n");
    gpu_init_done = 1;
}

void virtio_gpu_flush(void) {
    if (gpu_base == 0)
        return;

    gpu_transfer_to_host_2d();
    gpu_resource_flush();
}

void virtio_gpu_flush_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (gpu_base == 0)
        return;
    if (x >= ARM_FB_WIDTH || y >= ARM_FB_HEIGHT)
        return;
    if (x + w > ARM_FB_WIDTH)
        w = ARM_FB_WIDTH - x;
    if (y + h > ARM_FB_HEIGHT)
        h = ARM_FB_HEIGHT - y;
    gpu_transfer_rect(x, y, w, h);
    gpu_flush_rect(x, y, w, h);
}
