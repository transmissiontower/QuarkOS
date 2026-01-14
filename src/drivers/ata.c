#include "drivers/ata.h"
#include "util/util.h"

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECTOR_CNT  0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE_HEAD  0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7

#define ATA_CMD_READ_PIO    0x20
#define ATA_STATUS_BSY      0x80
#define ATA_STATUS_DRQ      0x08

void ata_wait_bsy() {
    while (in_port_B(ATA_STATUS) & ATA_STATUS_BSY);
}

void ata_wait_drq() {
    while (!(in_port_B(ATA_STATUS) & ATA_STATUS_DRQ));
}

void ata_read_sector(uint32_t lba, uint8_t *buffer) {
    ata_wait_bsy();

    out_port_B(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    
    out_port_B(ATA_ERROR, 0x00);
    
    out_port_B(ATA_SECTOR_CNT, 1);
    
    out_port_B(ATA_LBA_LO, (uint8_t)(lba & 0xFF));
    out_port_B(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    out_port_B(ATA_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    
    out_port_B(ATA_COMMAND, ATA_CMD_READ_PIO);

    ata_wait_bsy();
    ata_wait_drq();

    uint16_t *target = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        target[i] = in_port_W(ATA_DATA);
    }
}
