// SPDX-License-Identifier: MIT
/*
 * src/drivers/sb16.c - Sound Blaster 16 driver with DMA-based 8-bit and 16-bit playback
 */
#include "drivers/sb16.h"
#include "arch/x86/idt.h"
#include "util/util.h"
#include "lib/stdlib.h"
#define SB16_BASE       0x220
#define SB16_MIXER_ADDR (SB16_BASE + 0x04)
#define SB16_MIXER_DATA (SB16_BASE + 0x05)
#define SB16_RESET      (SB16_BASE + 0x06)
#define SB16_READ       (SB16_BASE + 0x0A)
#define SB16_WRITE      (SB16_BASE + 0x0C)
#define SB16_READ_STAT  (SB16_BASE + 0x0E)
#define SB16_INT16_ACK  (SB16_BASE + 0x0F)
#define SB16_IRQ        5
#define DMA2_MASK       0xD4
#define DMA2_MODE       0xD6
#define DMA2_FLIPFLOP   0xD8
#define DMA2_CH5_ADDR   0xC4
#define DMA2_CH5_COUNT  0xC6
#define DMA2_CH5_PAGE   0x8B
#define DMA_BUF_SIZE    (32 * 1024)
static uint8_t __attribute__((aligned(32768))) sb16_dma_buf[DMA_BUF_SIZE];
#define VIRT_TO_PHYS(v) ((uint32_t)(v) - 0xC0000000u)
static volatile int sb16_done   = 1;
static volatile int sb16_active = 0;
static uint8_t      sb16_irq_reg = 0;
static void dsp_write(uint8_t val)
{
    for (int i = 0; i < 100000; i++)
        if (!(inb(SB16_WRITE) & 0x80)) { outb(SB16_WRITE, val); return; }
}
static uint8_t dsp_read(void)
{
    for (int i = 0; i < 100000; i++)
        if (inb(SB16_READ_STAT) & 0x80) return inb(SB16_READ);
    return 0xFF;
}
static int dsp_reset(void)
{
    outb(SB16_RESET, 1);
    for (volatile int i = 0; i < 1000; i++) {}
    outb(SB16_RESET, 0);
    for (int i = 0; i < 100000; i++)
        if ((inb(SB16_READ_STAT) & 0x80) && (uint8_t)inb(SB16_READ) == 0xAAu) return 0;
    return -1;
}
static void sb16_irq_handler(struct InterruptRegisters *r)
{
    (void)r;
    outb(SB16_MIXER_ADDR, 0x82);
    uint8_t irq_stat = inb(SB16_MIXER_DATA);
    if (irq_stat & 0x02) {
        inb(SB16_INT16_ACK);
    } else {
        inb(SB16_READ_STAT);
    }
    sb16_done = 1;
}
static void dma_program_ch5(uint32_t phys, uint32_t bytes)
{
    uint32_t words      = bytes / 2;
    uint32_t word_addr  = (phys >> 1) & 0xFFFF;
    uint8_t  page       = (uint8_t)((phys >> 17) & 0xFF);
    outb(DMA2_MASK, 0x05);
    outb(DMA2_FLIPFLOP, 0x00);
    outb(DMA2_CH5_ADDR, (uint8_t)(word_addr & 0xFF));
    outb(DMA2_CH5_ADDR, (uint8_t)((word_addr >> 8) & 0xFF));
    outb(DMA2_CH5_PAGE, page);
    outb(DMA2_FLIPFLOP, 0x00);
    uint16_t count = (uint16_t)(words - 1);
    outb(DMA2_CH5_COUNT, (uint8_t)(count & 0xFF));
    outb(DMA2_CH5_COUNT, (uint8_t)((count >> 8) & 0xFF));
    outb(DMA2_MODE, 0x49);
    outb(DMA2_MASK, 0x01);
}
int drv_sb16_init(void)
{
    if (dsp_reset() != 0)
        return -1;
    dsp_write(0xE1);
    uint8_t major = dsp_read();
    uint8_t minor = dsp_read();
    if (major < 4)
        return -1;
    (void)minor;
    outb(SB16_MIXER_ADDR, 0x80);
    sb16_irq_reg = inb(SB16_MIXER_DATA);
    outb(SB16_MIXER_ADDR, 0x22); outb(SB16_MIXER_DATA, 0xFF);
    outb(SB16_MIXER_ADDR, 0x04); outb(SB16_MIXER_DATA, 0xFF);
    dsp_write(0xD1);
    irq_install_handler(SB16_IRQ, sb16_irq_handler);
    sb16_done   = 1;
    sb16_active = 1;
    return 0;
}
int drv_sb16_play(const void *virt_buf, uint32_t bytes,
                  uint32_t rate, uint8_t channels, uint8_t bits)
{
    if (!sb16_active || !sb16_done)
        return -1;
    if (!virt_buf || bytes == 0)
        return -1;
    if (bytes > DMA_BUF_SIZE)
        bytes = DMA_BUF_SIZE;
    uint32_t frame = (uint32_t)(bits / 8) * channels;
    if (frame > 1) bytes = (bytes / frame) * frame;
    if (bytes == 0) return -1;
    memcpy(sb16_dma_buf, virt_buf, bytes);
    uint32_t phys = VIRT_TO_PHYS(sb16_dma_buf);
    sb16_done = 0;
    dsp_write(0x41);
    dsp_write((uint8_t)((rate >> 8) & 0xFF));
    dsp_write((uint8_t)(rate & 0xFF));
    if (bits == 16) {
        dma_program_ch5(phys, bytes);
        uint8_t mode = 0x10;
        if (channels == 2) mode |= 0x20;
        uint16_t samples = (uint16_t)((bytes / (bits / 8)) - 1);
        dsp_write(0xB6);
        dsp_write(mode);
        dsp_write((uint8_t)(samples & 0xFF));
        dsp_write((uint8_t)((samples >> 8) & 0xFF));
    } else {
        outb(0x0A, 0x05);
        outb(0x0C, 0x00);
        outb(0x02, (uint8_t)(phys & 0xFF));
        outb(0x02, (uint8_t)((phys >> 8) & 0xFF));
        outb(0x83, (uint8_t)((phys >> 16) & 0xFF));
        outb(0x0C, 0x00);
        uint16_t count = (uint16_t)(bytes - 1);
        outb(0x03, (uint8_t)(count & 0xFF));
        outb(0x03, (uint8_t)((count >> 8) & 0xFF));
        outb(0x0B, 0x49);
        outb(0x0A, 0x01);
        uint8_t mode = 0x00;
        if (channels == 2) mode |= 0x20;
        uint16_t samples = (uint16_t)(bytes - 1);
        dsp_write(0xC6);
        dsp_write(mode);
        dsp_write((uint8_t)(samples & 0xFF));
        dsp_write((uint8_t)((samples >> 8) & 0xFF));
    }
    return 0;
}
int drv_sb16_ready(void)
{
    return sb16_done;
}
