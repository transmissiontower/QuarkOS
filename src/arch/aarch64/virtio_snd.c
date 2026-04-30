// SPDX-License-Identifier: MIT
/*
 * src/arch/aarch64/virtio_snd.c - VirtIO Sound driver (mirrors GPU driver pattern)
 */
#include "arch/aarch64/platform.h"
#include "util/util.h"
#include "lib/stdlib.h"

extern void arm_uart_puts(const char *s);
extern void arm_uart_putc(char c);

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_MMIO_GUEST_PAGE_SZ 0x028
#define VIRTIO_MMIO_QUEUE_ALIGN 0x03c
#define VIRTIO_MMIO_QUEUE_PFN 0x040
#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

#define VIRTIO_SND_DEVICE_ID 25
#define VIRTIO_SND_R_PCM_SET_PARAMS 0x0200
#define VIRTIO_SND_R_PCM_PREPARE 0x0300
#define VIRTIO_SND_R_PCM_START 0x0500
#define VIRTIO_SND_S_OK 0

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

#define QUEUE_SIZE 16
static uint32_t snd_base = 0;
static uint32_t snd_version = 0;
static int snd_ok = 0;
static int snd_stream_running = 0;

static uint8_t ctrl_vq_mem[8192] __attribute__((aligned(4096)));
static uint8_t tx_vq_mem[8192] __attribute__((aligned(4096)));

#define VQ_DESC(mem) ((struct virtq_desc *)(mem))

static volatile struct virtq_avail *ctrl_avail;
static volatile struct virtq_used *ctrl_used;
static uint16_t ctrl_last_used = 0;
static uint32_t ctrl_free = 0;

static volatile struct virtq_avail *tx_avail;
static volatile struct virtq_used *tx_used;
static uint16_t tx_last_used = 0;
static uint32_t tx_free = 0;

static uint8_t cmd_buf[512] __attribute__((aligned(4096)));
static uint8_t rsp_buf[512] __attribute__((aligned(4096)));
static uint8_t snd_bounce[8192] __attribute__((aligned(4096)));

static uint32_t tx_xfer_hdr[1] __attribute__((aligned(4096)));
static uint32_t tx_status[2] __attribute__((aligned(4096)));

static inline uint32_t va_to_pa(void *va) {
    return (uint32_t)(uintptr_t)va - 0xC0000000 + 0x40000000;
}

static void vq_init(struct virtq_desc *d, uint32_t *free) {
    for (int i = 0; i < QUEUE_SIZE; i++) {
        d[i].addr = 0;
        d[i].len = 0;
        d[i].flags = 0;
        d[i].next = (i + 1 < QUEUE_SIZE) ? i + 1 : 0xFFFF;
    }
    *free = 0;
}
static uint16_t alloc_d(struct virtq_desc *d, uint32_t *free) {
    if (*free >= QUEUE_SIZE)
        return 0xFFFF;
    uint16_t i = (uint16_t)*free;
    *free = d[i].next;
    return i;
}
static void free_d(struct virtq_desc *d, uint32_t *free, uint16_t h) {
    uint16_t i = h;
    for (;;) {
        uint16_t n = d[i].next;
        int has = d[i].flags & VIRTQ_DESC_F_NEXT;
        d[i].next = (uint16_t)*free;
        *free = i;
        if (!has)
            break;
        i = n;
    }
}

static int setup_vq(uint32_t base, uint32_t ver, int idx, uint8_t *mem8k,
                    volatile struct virtq_avail **av, volatile struct virtq_used **us,
                    uint32_t *fr) {
    struct virtq_desc *desc = (struct virtq_desc *)mem8k;
    mmio_write32(base + VIRTIO_MMIO_QUEUE_SEL, idx);
    uint32_t max = mmio_read32(base + VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max == 0)
        return -1;
    uint32_t num = QUEUE_SIZE;
    if (num > max)
        num = max;
    mmio_write32(base + VIRTIO_MMIO_QUEUE_NUM, num);

    memset(mem8k, 0, 8192);
    vq_init(desc, fr);


    *av = (volatile struct virtq_avail *)(mem8k + QUEUE_SIZE * sizeof(struct virtq_desc));
    (*av)->flags = 0;
    (*av)->idx = 0;
    *us = (volatile struct virtq_used *)(mem8k + 4096);
    (*us)->flags = 0;
    (*us)->idx = 0;

    uint32_t dp = va_to_pa(mem8k);

    if (ver == 2) {
        uint32_t ap = va_to_pa((void *)*av);
        uint32_t up = va_to_pa(mem8k + 4096);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DESC_LOW, dp);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DESC_HIGH, 0);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DRIVER_LOW, ap);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DRIVER_HIGH, 0);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DEVICE_LOW, up);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_DEVICE_HIGH, 0);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_READY, 1);
    } else {
        mmio_write32(base + VIRTIO_MMIO_GUEST_PAGE_SZ, 4096);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_ALIGN, 4096);
        mmio_write32(base + VIRTIO_MMIO_QUEUE_PFN, dp >> 12);
    }
    return 0;
}

static int snd_ctrl(void *req, uint32_t rlen, void *rsp, uint32_t slen) {

    for (uint32_t i = 0; i < rlen; i += 64)
        asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)((uint8_t *)req + i)));
    arm_dsb();

    uint16_t d0 = alloc_d(VQ_DESC(ctrl_vq_mem), &ctrl_free);
    uint16_t d1 = alloc_d(VQ_DESC(ctrl_vq_mem), &ctrl_free);
    if (d0 == 0xFFFF || d1 == 0xFFFF)
        return -1;

    VQ_DESC(ctrl_vq_mem)[d0].addr = (uint64_t)va_to_pa(req);
    VQ_DESC(ctrl_vq_mem)[d0].len = rlen;
    VQ_DESC(ctrl_vq_mem)[d0].flags = VIRTQ_DESC_F_NEXT;
    VQ_DESC(ctrl_vq_mem)[d0].next = d1;
    VQ_DESC(ctrl_vq_mem)[d1].addr = (uint64_t)va_to_pa(rsp);
    VQ_DESC(ctrl_vq_mem)[d1].len = slen;
    VQ_DESC(ctrl_vq_mem)[d1].flags = VIRTQ_DESC_F_WRITE;
    VQ_DESC(ctrl_vq_mem)[d1].next = 0;


    asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&VQ_DESC(ctrl_vq_mem)[d0]));
    asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&VQ_DESC(ctrl_vq_mem)[d1]));
    arm_dsb();

    uint16_t ai = ctrl_avail->idx;
    ctrl_avail->ring[ai % QUEUE_SIZE] = d0;
    arm_dsb();
    ctrl_avail->idx = ai + 1;
    asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&ctrl_avail->idx));
    asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&ctrl_avail->ring[ai % QUEUE_SIZE]));
    arm_dsb();

    mmio_write32(snd_base + VIRTIO_MMIO_QUEUE_NOTIFY, 0);


    for (volatile int j = 0; j < 2000000; j++) {
        asm volatile("dc ivac, %0" ::"r"((uint64_t)(uintptr_t)&ctrl_used->idx) : "memory");
        arm_dmb();
        if (ctrl_used->idx != ctrl_last_used) {
            ctrl_last_used++;
            break;
        }
    }


    for (uint32_t i = 0; i < slen; i += 64)
        asm volatile("dc ivac, %0" ::"r"((uint64_t)(uintptr_t)((uint8_t *)rsp + i)) : "memory");
    arm_dsb();

    free_d(VQ_DESC(ctrl_vq_mem), &ctrl_free, d0);

    uint32_t code = *(volatile uint32_t *)rsp;
    return (code == VIRTIO_SND_S_OK) ? 0 : (int)code;
}

void virtio_snd_init(void) {
    snd_base = 0;
    for (uint32_t i = 0; i < VIRTIO_MMIO_COUNT; i++) {
        uint32_t b = VIRTIO_MMIO_VIRT(i);
        if (mmio_read32(b + VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976)
            continue;
        uint32_t v = mmio_read32(b + VIRTIO_MMIO_VERSION);
        uint32_t d = mmio_read32(b + VIRTIO_MMIO_DEVICE_ID);
        if (d != VIRTIO_SND_DEVICE_ID)
            continue;
        if (v != 1 && v != 2)
            continue;
        snd_base = b;
        snd_version = v;
        break;
    }
    if (!snd_base) {
        arm_uart_puts("virtio-snd: no device\n");
        return;
    }
    arm_uart_puts("virtio-snd: found device, version=");
    arm_uart_putc('0' + snd_version);
    arm_uart_putc('\n');


    mmio_write32(snd_base + VIRTIO_MMIO_STATUS, 0);
    mmio_write32(snd_base + VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    mmio_write32(snd_base + VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    mmio_write32(snd_base + VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    (void)mmio_read32(snd_base + VIRTIO_MMIO_DEVICE_FEATURES);
    mmio_write32(snd_base + VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    mmio_write32(snd_base + VIRTIO_MMIO_DRIVER_FEATURES, 0);
    if (snd_version == 2) {
        mmio_write32(snd_base + VIRTIO_MMIO_STATUS,
                     VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
        if (!(mmio_read32(snd_base + VIRTIO_MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK)) {
            arm_uart_puts("virtio-snd: features fail\n");
            return;
        }
    }




    if (setup_vq(snd_base, snd_version, 0, ctrl_vq_mem, &ctrl_avail, &ctrl_used, &ctrl_free) < 0) {
        arm_uart_puts("virtio-snd: ctrl VQ fail\n");
        return;
    }


    {
        static uint8_t ev_mem[8192] __attribute__((aligned(4096)));
        volatile struct virtq_avail *ea;
        volatile struct virtq_used *eu;
        uint32_t ef;
        setup_vq(snd_base, snd_version, 1, ev_mem, &ea, &eu, &ef);
    }


    if (setup_vq(snd_base, snd_version, 2, tx_vq_mem, &tx_avail, &tx_used, &tx_free) < 0) {
        arm_uart_puts("virtio-snd: TX VQ fail\n");
        return;
    }

    uint32_t st = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK;
    if (snd_version == 2)
        st |= VIRTIO_STATUS_FEATURES_OK;
    mmio_write32(snd_base + VIRTIO_MMIO_STATUS, st);

    snd_ok = 1;
    arm_uart_puts("virtio-snd: initialized\n");


    memset(cmd_buf, 0, 64);
    uint32_t *p = (uint32_t *)cmd_buf;
    p[0] = VIRTIO_SND_R_PCM_SET_PARAMS;
    p[1] = 0;
    p[2] = 16384;
    p[3] = 4096;
    p[4] = 0;
    cmd_buf[20] = 2;
    cmd_buf[21] = 5;
    cmd_buf[22] = 7;
    cmd_buf[23] = 0;

    memset(rsp_buf, 0, 64);
    int rc = snd_ctrl(cmd_buf, 24, rsp_buf, 4);
    arm_uart_puts("virtio-snd: SET_PARAMS rc=");
    if (rc < 0)
        arm_uart_putc('-');
    arm_uart_putc('0' + (rc < 0 ? -rc : rc) % 10);
    arm_uart_puts(" rsp=");
    for (int i = 0; i < 8; i++) {
        uint8_t b = ((volatile uint8_t *)rsp_buf)[i];
        arm_uart_putc("0123456789abcdef"[b >> 4]);
        arm_uart_putc("0123456789abcdef"[b & 0xf]);
        arm_uart_putc(' ');
    }
    arm_uart_putc('\n');

    if (rc == 0) {

        memset(cmd_buf, 0, 16);
        p[0] = VIRTIO_SND_R_PCM_PREPARE;
        p[1] = 0;
        memset(rsp_buf, 0, 64);
        rc = snd_ctrl(cmd_buf, 8, rsp_buf, 4);
        arm_uart_puts("virtio-snd: PREPARE rc=");
        arm_uart_putc('0' + (rc < 0 ? 1 : 0));
        arm_uart_putc('\n');

        if (rc == 0) {

            memset(cmd_buf, 0, 16);
            p[0] = VIRTIO_SND_R_PCM_START;
            p[1] = 0;
            memset(rsp_buf, 0, 64);
            rc = snd_ctrl(cmd_buf, 8, rsp_buf, 4);
            arm_uart_puts("virtio-snd: START rc=");
            arm_uart_putc('0' + (rc < 0 ? 1 : 0));
            arm_uart_putc('\n');
            if (rc == 0)
                snd_stream_running = 1;
        }
    }
}

int virtio_snd_play(const void *buf, uint32_t len, uint32_t rate, uint8_t channels, uint8_t bits) {
    (void)rate;
    (void)channels;
    (void)bits;
    arm_uart_puts("virtio-snd: play called len=");
    {
        char n[12];
        int ni = 0;
        uint32_t v = len;
        if (v == 0)
            n[ni++] = '0';
        else {
            while (v) {
                n[ni++] = '0' + (v % 10);
                v /= 10;
            }
        }
        for (int j = ni - 1; j >= 0; j--)
            arm_uart_putc(n[j]);
    }
    arm_uart_putc('\n');
    if (!snd_ok || !snd_stream_running) {
        arm_uart_puts("virtio-snd: not ready\n");
        return -1;
    }
    if (!buf || !len)
        return 0;

    const uint8_t *src = (const uint8_t *)buf;
    uint32_t sent = 0;
    while (sent < len) {
        uint32_t chunk = len - sent;
        if (chunk > 8192)
            chunk = 8192;


        memcpy(snd_bounce, src + sent, chunk);
        for (uint32_t i = 0; i < chunk; i += 64)
            asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)(snd_bounce + i)));

        tx_xfer_hdr[0] = 0;
        tx_status[0] = 0;
        tx_status[1] = 0;
        asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)tx_xfer_hdr));
        asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)tx_status));
        arm_dsb();

        uint16_t d0 = alloc_d(VQ_DESC(tx_vq_mem), &tx_free);
        uint16_t d1 = alloc_d(VQ_DESC(tx_vq_mem), &tx_free);
        uint16_t d2 = alloc_d(VQ_DESC(tx_vq_mem), &tx_free);
        if (d0 == 0xFFFF || d1 == 0xFFFF || d2 == 0xFFFF)
            break;

        VQ_DESC(tx_vq_mem)[d0].addr = (uint64_t)va_to_pa(tx_xfer_hdr);
        VQ_DESC(tx_vq_mem)[d0].len = 4;
        VQ_DESC(tx_vq_mem)[d0].flags = VIRTQ_DESC_F_NEXT;
        VQ_DESC(tx_vq_mem)[d0].next = d1;

        VQ_DESC(tx_vq_mem)[d1].addr = (uint64_t)va_to_pa(snd_bounce);
        VQ_DESC(tx_vq_mem)[d1].len = chunk;
        VQ_DESC(tx_vq_mem)[d1].flags = VIRTQ_DESC_F_NEXT;
        VQ_DESC(tx_vq_mem)[d1].next = d2;

        VQ_DESC(tx_vq_mem)[d2].addr = (uint64_t)va_to_pa(tx_status);
        VQ_DESC(tx_vq_mem)[d2].len = 8;
        VQ_DESC(tx_vq_mem)[d2].flags = VIRTQ_DESC_F_WRITE;
        VQ_DESC(tx_vq_mem)[d2].next = 0;

        asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&VQ_DESC(tx_vq_mem)[d0]));
        asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&VQ_DESC(tx_vq_mem)[d1]));
        asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&VQ_DESC(tx_vq_mem)[d2]));
        arm_dsb();

        uint16_t ai = tx_avail->idx;
        tx_avail->ring[ai % QUEUE_SIZE] = d0;
        arm_dsb();
        tx_avail->idx = ai + 1;
        asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&tx_avail->idx));
        asm volatile("dc cvac, %0" ::"r"((uint64_t)(uintptr_t)&tx_avail->ring[ai % QUEUE_SIZE]));
        arm_dsb();

        mmio_write32(snd_base + VIRTIO_MMIO_QUEUE_NOTIFY, 2);


        for (volatile int j = 0; j < 2000000; j++) {
            asm volatile("dc ivac, %0" ::"r"((uint64_t)(uintptr_t)&tx_used->idx) : "memory");
            arm_dmb();
            if (tx_used->idx != tx_last_used) {
                tx_last_used++;
                break;
            }
        }

        free_d(VQ_DESC(tx_vq_mem), &tx_free, d0);
        sent += chunk;
    }
    arm_uart_puts("virtio-snd: play done\n");
    return 0;
}

int virtio_snd_ready(void) {
    return snd_ok && snd_stream_running;
}
