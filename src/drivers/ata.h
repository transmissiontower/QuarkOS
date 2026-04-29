// SPDX-License-Identifier: MIT
/*
 * src/drivers/ata.h - ATA disk driver interface for LBA read/write operations
 */
#ifndef ATA_H
#define ATA_H
#include "lib/stdint.h"
void drv_stor_ata_init();
void drv_stor_ata_read(uint32_t lba, uint8_t *buffer);
void drv_stor_ata_write(uint32_t lba, uint8_t *buffer);
#endif
