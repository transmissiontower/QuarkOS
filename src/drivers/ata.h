#ifndef ATA_H
#define ATA_H

#include "lib/stdint.h"

void ata_init();
void ata_read_sector(uint32_t lba, uint8_t *buffer);
void ata_write_sector(uint32_t lba, uint8_t *buffer);

#endif