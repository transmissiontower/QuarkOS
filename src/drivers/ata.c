// SPDX-License-Identifier: MIT
/*
 * src/drivers/ata.c - PIO-mode ATA disk driver for single-sector reads
 */
#include "drivers/ata.h"
#include "arch/x86/io.h"

#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECTOR_CNT 0x1F2
#define ATA_LBA_LO     0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HI     0x1F5
#define ATA_DRIVE_HEAD 0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7

#define ATA_STATUS_BSY 0x80
#define ATA_STATUS_DRQ 0x08

static void ata_wait_ready(void)
{
    while (io_inb(ATA_STATUS) & ATA_STATUS_BSY) {}
}

void drv_stor_ata_init(void)
{
    ata_wait_ready();
}

void drv_stor_ata_read(uint32_t lba, uint8_t *buffer)
{
    ata_wait_ready();
    io_outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    io_outb(ATA_ERROR, 0x00);
    io_outb(ATA_SECTOR_CNT, 1);
    io_outb(ATA_LBA_LO,  (uint8_t)(lba));
    io_outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    io_outb(ATA_LBA_HI,  (uint8_t)(lba >> 16));
    io_outb(ATA_COMMAND, 0x20);
    ata_wait_ready();
    while (!(io_inb(ATA_STATUS) & ATA_STATUS_DRQ)) {}
    uint16_t *dst = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++)
        dst[i] = io_inw(ATA_DATA);
}

void drv_stor_ata_write(uint32_t lba, uint8_t *buffer)
{
    (void)lba;
    (void)buffer;
}
