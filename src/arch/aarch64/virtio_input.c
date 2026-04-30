// SPDX-License-Identifier: MIT
/*
 * src/arch/aarch64/virtio_input.c - VirtIO input driver for keyboard and mouse events
 */
#include "arch/aarch64/platform.h"
#include "kernel/klog.h"
#include "util/util.h"

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8

#define VIRTIO_MMIO_QUEUE_PFN 0x040
#define VIRTIO_MMIO_QUEUE_ALIGN 0x03c
#define VIRTIO_MMIO_GUEST_PAGE_SZ 0x028

#define VIRTIO_INPUT_CFG_SELECT 0x100
#define VIRTIO_INPUT_CFG_SUBSEL 0x101
#define VIRTIO_INPUT_CFG_SIZE 0x102
#define VIRTIO_INPUT_CFG_DATA 0x108

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

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03

#define REL_X 0x00
#define REL_Y 0x01

#define ABS_X 0x00
#define ABS_Y 0x01

#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112

#define VIRTIO_INPUT_CFG_EV_BITS 0x11

struct virtio_input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} __attribute__((packed));

#define INPUT_QUEUE_SIZE 32
#define MAX_INPUT_DEVS 4

typedef enum {
    INPUT_DEV_UNKNOWN = 0,
    INPUT_DEV_KEYBOARD,
    INPUT_DEV_POINTER_REL,
    INPUT_DEV_POINTER_ABS
} input_dev_kind_t;

struct input_dev {
    uint32_t base;
    uint32_t version;
    uint32_t irq;
    int active;
    input_dev_kind_t kind;

    struct virtq_desc desc[INPUT_QUEUE_SIZE] __attribute__((aligned(4096)));
    uint8_t avail_buf[4096] __attribute__((aligned(4096)));
    uint8_t used_buf[4096] __attribute__((aligned(4096)));

    struct virtio_input_event evts[INPUT_QUEUE_SIZE] __attribute__((aligned(64)));

    volatile struct virtq_avail *avail;
    volatile struct virtq_used *used;
    uint16_t last_used;
};

static struct input_dev input_devs[MAX_INPUT_DEVS];
static int num_input_devs;

static int kbd_irq_num = -1;
static int mouse_irq_num = -1;

static int bitmap_has_bit(const uint8_t *bitmap, uint32_t size, uint16_t bit) {
    uint32_t byte = bit >> 3;
    uint8_t mask = (uint8_t)(1u << (bit & 7));
    if (byte >= size)
        return 0;
    return (bitmap[byte] & mask) != 0;
}

static uint32_t virtio_input_cfg_read_bitmap(uint32_t base, uint8_t select, uint8_t subsel,
                                             uint8_t *out, uint32_t out_size) {
    if (!out || out_size == 0)
        return 0;
    memset(out, 0, out_size);
    mmio_write8(base + VIRTIO_INPUT_CFG_SELECT, select);
    mmio_write8(base + VIRTIO_INPUT_CFG_SUBSEL, subsel);
    uint32_t size = mmio_read8(base + VIRTIO_INPUT_CFG_SIZE);
    if (size > out_size)
        size = out_size;
    for (uint32_t i = 0; i < size; i++)
        out[i] = mmio_read8(base + VIRTIO_INPUT_CFG_DATA + i);
    return size;
}

static input_dev_kind_t detect_input_dev_kind(uint32_t base) {
    uint8_t bitmap[32];
    uint32_t rel_sz = virtio_input_cfg_read_bitmap(base, VIRTIO_INPUT_CFG_EV_BITS, EV_REL, bitmap,
                                                   sizeof(bitmap));
    if (rel_sz && (bitmap_has_bit(bitmap, rel_sz, REL_X) || bitmap_has_bit(bitmap, rel_sz, REL_Y)))
        return INPUT_DEV_POINTER_REL;

    uint32_t abs_sz = virtio_input_cfg_read_bitmap(base, VIRTIO_INPUT_CFG_EV_BITS, EV_ABS, bitmap,
                                                   sizeof(bitmap));
    if (abs_sz && (bitmap_has_bit(bitmap, abs_sz, ABS_X) || bitmap_has_bit(bitmap, abs_sz, ABS_Y)))
        return INPUT_DEV_POINTER_ABS;

    uint32_t key_sz = virtio_input_cfg_read_bitmap(base, VIRTIO_INPUT_CFG_EV_BITS, EV_KEY, bitmap,
                                                   sizeof(bitmap));
    if (key_sz)
        return INPUT_DEV_KEYBOARD;

    return INPUT_DEV_UNKNOWN;
}

static inline uint32_t va_to_pa(void *va) {
    return (uint32_t)va - 0xC0000000 + 0x40000000;
}

static const uint8_t linux_to_ps2[128] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
    0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
    0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c,
    0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b,
    0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
    0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x00, 0x00, 0x56, 0x57, 0x58,

    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0};

static uint8_t linux_to_ps2_extended(uint16_t code) {
    switch (code) {
    case 96:
        return 0x1C;
    case 97:
        return 0x1D;
    case 98:
        return 0x35;
    case 100:
        return 0x38;
    case 102:
        return 0x47;
    case 103:
        return 0x48;
    case 104:
        return 0x49;
    case 105:
        return 0x4B;
    case 106:
        return 0x4D;
    case 107:
        return 0x4F;
    case 108:
        return 0x50;
    case 109:
        return 0x51;
    case 110:
        return 0x52;
    case 111:
        return 0x53;
    default:
        return 0;
    }
}

#define SCANCODE_BUF_SIZE 4096

static volatile uint8_t scancode_buf[SCANCODE_BUF_SIZE];
static volatile uint32_t sc_head;
static volatile uint32_t sc_tail;

static void scancode_push(uint8_t sc) {
    uint32_t next = (sc_tail + 1) % SCANCODE_BUF_SIZE;
    if (next == sc_head)
        return;
    scancode_buf[sc_tail] = sc;
    sc_tail = next;
}

#define MOUSE_BUF_SIZE 4096

static volatile uint8_t mouse_buf[MOUSE_BUF_SIZE];
static volatile uint32_t mb_head;
static volatile uint32_t mb_tail;

static void mouse_push(uint8_t b) {
    uint32_t next = (mb_tail + 1) % MOUSE_BUF_SIZE;
    if (next == mb_head)
        return;
    mouse_buf[mb_tail] = b;
    mb_tail = next;
}

int virtio_input_kbd_has_data(void) {
    return sc_head != sc_tail;
}

uint8_t virtio_input_kbd_read(void) {
    if (sc_head == sc_tail)
        return 0;
    uint8_t sc = scancode_buf[sc_head];
    sc_head = (sc_head + 1) % SCANCODE_BUF_SIZE;
    return sc;
}

int virtio_input_mouse_has_data(void) {
    return mb_head != mb_tail;
}

uint8_t virtio_input_mouse_read(void) {
    if (mb_head == mb_tail)
        return 0;
    uint8_t b = mouse_buf[mb_head];
    mb_head = (mb_head + 1) % MOUSE_BUF_SIZE;
    return b;
}

static int32_t accum_dx;
static int32_t accum_dy;
static uint8_t btn_state;
static int mouse_dirty;

static int32_t abs_prev_x = -1;
static int32_t abs_prev_y = -1;

#define ABS_COORD_MAX 0x7fff

static int32_t scale_abs_coord(uint32_t value, int32_t size) {
    if (size <= 1)
        return 0;
    if (value > ABS_COORD_MAX)
        value = ABS_COORD_MAX;
    return (int32_t)((value * (uint32_t)(size - 1)) / ABS_COORD_MAX);
}

static void flush_mouse_packet(void) {
    if (!mouse_dirty)
        return;

    int32_t dx = accum_dx;
    int32_t dy = accum_dy;
    accum_dx = 0;
    accum_dy = 0;
    mouse_dirty = 0;

    if (dx > 255)
        dx = 255;
    if (dx < -256)
        dx = -256;
    if (dy > 255)
        dy = 255;
    if (dy < -256)
        dy = -256;

    uint8_t b0 = 0x08;
    if (btn_state & 0x01)
        b0 |= 0x01;
    if (btn_state & 0x02)
        b0 |= 0x02;
    if (btn_state & 0x04)
        b0 |= 0x04;
    if (dx < 0)
        b0 |= 0x10;
    if (dy > 0)
        b0 |= 0x20;

    mouse_push(b0);
    mouse_push((uint8_t)(dx & 0xFF));
    mouse_push((uint8_t)((-dy) & 0xFF));
    mouse_push(0);
}

static int input_dev_init(struct input_dev *dev) {
    uint32_t base = dev->base;
    uint32_t version = dev->version;

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
    uint32_t num = INPUT_QUEUE_SIZE;
    if (num > max)
        num = max;

    mmio_write32(base + VIRTIO_MMIO_QUEUE_NUM, num);

    for (uint32_t i = 0; i < INPUT_QUEUE_SIZE; i++) {
        dev->desc[i].next = (uint16_t)(i + 1);
        dev->desc[i].flags = 0;
    }

    uint32_t desc_pa = va_to_pa((void *)dev->desc);
    uint32_t avail_pa = va_to_pa((void *)dev->avail_buf);
    uint32_t used_pa = va_to_pa((void *)dev->used_buf);

    if (version == 2) {
        dev->avail = (volatile struct virtq_avail *)dev->avail_buf;
        dev->used = (volatile struct virtq_used *)dev->used_buf;
        dev->avail->flags = 0;
        dev->avail->idx = 0;
        dev->used->flags = 0;
        dev->used->idx = 0;

        mmio_write32(base + VIRTIO_MMIO_QUEUE_DESC_LOW, desc_pa);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DESC_HIGH, 0);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DRIVER_LOW, avail_pa);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DRIVER_HIGH, 0);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DEVICE_LOW, used_pa);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DEVICE_HIGH, 0);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_READY, 1);
    } else {
        dev->avail = (volatile struct virtq_avail *)((uint8_t *)dev->desc +
                                                     INPUT_QUEUE_SIZE * sizeof(struct virtq_desc));
        dev->avail->flags = 0;
        dev->avail->idx = 0;

        dev->used = (volatile struct virtq_used *)((uint8_t *)dev->desc + 4096);
        dev->used->flags = 0;
        dev->used->idx = 0;

        mmio_write32(base + VIRTIO_MMIO_GUEST_PAGE_SZ, 4096);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_ALIGN, 4096);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_PFN, desc_pa >> 12);
    }

    dev->last_used = 0;

    for (uint32_t i = 0; i < INPUT_QUEUE_SIZE; i++) {
        dev->desc[i].addr = (uint64_t)va_to_pa((void *)&dev->evts[i]);
        dev->desc[i].len = sizeof(struct virtio_input_event);
        dev->desc[i].flags = VIRTQ_DESC_F_WRITE;
        dev->desc[i].next = 0;

        dev->avail->ring[i] = (uint16_t)i;
    }
    arm_dsb();
    dev->avail->idx = INPUT_QUEUE_SIZE;
    arm_dsb();

    {
        uint64_t d = (uint64_t)(uintptr_t)dev->desc;
        for (uint64_t off = 0; off < sizeof(dev->desc); off += 64)
            asm volatile("dc cvac, %0" ::"r"(d + off) : "memory");

        uint64_t a = (uint64_t)(uintptr_t)dev->avail;
        for (uint64_t off = 0; off < 256; off += 64)
            asm volatile("dc cvac, %0" ::"r"(a + off) : "memory");

        uint64_t u = (uint64_t)(uintptr_t)dev->used;
        for (uint64_t off = 0; off < 512; off += 64)
            asm volatile("dc cvac, %0" ::"r"(u + off) : "memory");

        uint64_t e = (uint64_t)(uintptr_t)dev->evts;
        for (uint64_t off = 0; off < sizeof(dev->evts); off += 64)
            asm volatile("dc cvac, %0" ::"r"(e + off) : "memory");
    }
    arm_dsb();

    uint32_t status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK;
    if (version == 2)
        status |= VIRTIO_STATUS_FEATURES_OK;
    mmio_write32(base + VIRTIO_MMIO_STATUS, status);

    mmio_write32(base + VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    dev->active = 1;
    return 0;
}

static void process_event(struct input_dev *dev, struct virtio_input_event *ev) {
    if (ev->type == EV_KEY) {
        uint16_t code = ev->code;
        int release = (ev->value == 0);

        if (code < BTN_LEFT || code > BTN_MIDDLE)
            KLOG_INPUT(LOG_INFO, "KEY code=%u %s", (uint32_t)code, release ? "UP" : "DOWN");

        if ((dev->kind == INPUT_DEV_POINTER_REL || dev->kind == INPUT_DEV_POINTER_ABS) &&
            code >= BTN_LEFT && code <= BTN_MIDDLE) {
            uint8_t bit = 0;
            if (code == BTN_LEFT)
                bit = 0x01;
            if (code == BTN_RIGHT)
                bit = 0x02;
            if (code == BTN_MIDDLE)
                bit = 0x04;

            if (release)
                btn_state &= ~bit;
            else
                btn_state |= bit;
            mouse_dirty = 1;
            flush_mouse_packet();
            return;
        }

        if (dev->kind == INPUT_DEV_KEYBOARD || dev->kind == INPUT_DEV_UNKNOWN) {
            uint8_t ext = linux_to_ps2_extended(code);
            if (ext) {
                scancode_push(0xE0);
                scancode_push(release ? (ext | 0x80) : ext);
            } else if (code < 128) {
                uint8_t sc = linux_to_ps2[code];
                if (sc)
                    scancode_push(release ? (sc | 0x80) : sc);
            }
        }
    } else if (ev->type == EV_REL && dev->kind == INPUT_DEV_POINTER_REL) {
        if (ev->code == REL_X) {
            accum_dx += (int32_t)ev->value;
            mouse_dirty = 1;
        } else if (ev->code == REL_Y) {
            accum_dy += (int32_t)ev->value;
            mouse_dirty = 1;
        }
    } else if (ev->type == EV_ABS && dev->kind == INPUT_DEV_POINTER_ABS) {
        if (ev->code == ABS_X) {
            int32_t new_x = scale_abs_coord(ev->value, ARM_FB_WIDTH);
            if (abs_prev_x >= 0) {
                accum_dx += new_x - abs_prev_x;
                mouse_dirty = 1;
            }
            abs_prev_x = new_x;
        } else if (ev->code == ABS_Y) {
            int32_t new_y = scale_abs_coord(ev->value, ARM_FB_HEIGHT);
            if (abs_prev_y >= 0) {
                accum_dy += new_y - abs_prev_y;
                mouse_dirty = 1;
            }
            abs_prev_y = new_y;
        }
    } else if (ev->type == EV_SYN &&
               (dev->kind == INPUT_DEV_POINTER_REL || dev->kind == INPUT_DEV_POINTER_ABS)) {
        flush_mouse_packet();
    }
}

static void input_dev_poll(struct input_dev *dev) {
    if (!dev->active)
        return;

    static volatile int polling_in_progress;
    uint32_t irq_flags = arm_irq_save();
    if (polling_in_progress) {
        arm_irq_restore(irq_flags);
        return;
    }
    polling_in_progress = 1;
    arm_irq_restore(irq_flags);

    uint32_t isr = mmio_read32(dev->base + VIRTIO_MMIO_INTERRUPT_STATUS);
    if (isr)
        mmio_write32(dev->base + VIRTIO_MMIO_INTERRUPT_ACK, isr);

    {
        uint64_t used_addr = (uint64_t)(uintptr_t)dev->used;

        for (uint64_t off = 0; off < 512; off += 64)
            asm volatile("dc ivac, %0" ::"r"(used_addr + off) : "memory");
        uint64_t evt_addr = (uint64_t)(uintptr_t)dev->evts;
        for (uint64_t off = 0; off < sizeof(dev->evts); off += 64)
            asm volatile("dc ivac, %0" ::"r"(evt_addr + off) : "memory");

        uint64_t desc_addr = (uint64_t)(uintptr_t)dev->desc;
        for (uint64_t off = 0; off < sizeof(dev->desc); off += 64)
            asm volatile("dc ivac, %0" ::"r"(desc_addr + off) : "memory");
    }
    arm_dsb();

    while (dev->last_used != dev->used->idx) {
        uint16_t ui = dev->last_used % INPUT_QUEUE_SIZE;
        uint32_t desc_idx = dev->used->ring[ui].id;

        if (desc_idx >= INPUT_QUEUE_SIZE) {
            dev->last_used++;
            continue;
        }

        process_event(dev, &dev->evts[desc_idx]);

        dev->desc[desc_idx].addr = (uint64_t)va_to_pa((void *)&dev->evts[desc_idx]);
        dev->desc[desc_idx].len = sizeof(struct virtio_input_event);
        dev->desc[desc_idx].flags = VIRTQ_DESC_F_WRITE;
        dev->desc[desc_idx].next = 0;

        uint16_t avail_idx = dev->avail->idx;
        dev->avail->ring[avail_idx % INPUT_QUEUE_SIZE] = (uint16_t)desc_idx;
        arm_dsb();
        dev->avail->idx = avail_idx + 1;
        arm_dsb();

        {
            uint64_t avail_addr = (uint64_t)(uintptr_t)dev->avail;
            for (uint64_t off = 0; off < 256; off += 64)
                asm volatile("dc cvac, %0" ::"r"(avail_addr + off) : "memory");
        }
        asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&dev->desc[desc_idx]) : "memory");
        arm_dsb();
        mmio_write32(dev->base + VIRTIO_MMIO_QUEUE_NOTIFY, 0);

        dev->last_used++;
    }

    polling_in_progress = 0;
}

int virtio_input_get_kbd_irq(void) {
    return kbd_irq_num;
}
int virtio_input_get_mouse_irq(void) {
    return mouse_irq_num;
}

void virtio_input_init(void) {
    num_input_devs = 0;
    kbd_irq_num = -1;
    mouse_irq_num = -1;
    sc_head = 0;
    sc_tail = 0;
    mb_head = 0;
    mb_tail = 0;
    accum_dx = 0;
    accum_dy = 0;
    btn_state = 0;
    mouse_dirty = 0;
    abs_prev_x = -1;
    abs_prev_y = -1;

    for (uint32_t i = 0; i < VIRTIO_MMIO_COUNT && num_input_devs < MAX_INPUT_DEVS; i++) {
        uint32_t base = VIRTIO_MMIO_VIRT(i);
        uint32_t magic = mmio_read32(base + VIRTIO_MMIO_MAGIC_VALUE);
        if (magic != 0x74726976)
            continue;

        uint32_t version = mmio_read32(base + VIRTIO_MMIO_VERSION);
        uint32_t dev_id = mmio_read32(base + VIRTIO_MMIO_DEVICE_ID);

        if (dev_id != 18)
            continue;

        if (version != 1 && version != 2)
            continue;

        struct input_dev *dev = &input_devs[num_input_devs];
        memset(dev, 0, sizeof(*dev));
        dev->base = base;
        dev->version = version;
        dev->irq = ARM_IRQ_VIRTIO_BASE + i;
        dev->active = 0;
        dev->kind = detect_input_dev_kind(base);

        if (input_dev_init(dev) == 0) {
            arm_gic_enable_irq(dev->irq);
            if (kbd_irq_num < 0 &&
                (dev->kind == INPUT_DEV_KEYBOARD || dev->kind == INPUT_DEV_UNKNOWN))
                kbd_irq_num = dev->irq;
            if (mouse_irq_num < 0 &&
                (dev->kind == INPUT_DEV_POINTER_REL || dev->kind == INPUT_DEV_POINTER_ABS))
                mouse_irq_num = dev->irq;
            num_input_devs++;
            arm_uart_puts("virtio-input: device initialised\n");
        }
    }

    if (num_input_devs == 0)
        arm_uart_puts("virtio-input: no devices found\n");
}

void virtio_input_poll(void) {
    for (int i = 0; i < num_input_devs; i++)
        input_dev_poll(&input_devs[i]);
}

void virtio_input_irq(uint32_t irq) {
    for (int i = 0; i < num_input_devs; i++) {
        if (input_devs[i].irq == irq && input_devs[i].active) {
            input_dev_poll(&input_devs[i]);
            return;
        }
    }
}
