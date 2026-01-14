#include "fs/fat.h"
#include "drivers/ata.h"
#include "video/window.h"
#include "lib/stdlib.h"
#include "mm/kmalloc.h"
#include "util/util.h"

uint32_t fat_start_lba;
uint32_t root_dir_lba;
uint32_t data_start_lba;
uint8_t  sectors_per_cluster;
uint16_t sectors_per_fat;
uint16_t root_dir_entries;

uint8_t sector_buffer[512];

void fat_init(){
    ata_read_sector(0, sector_buffer);
    fat_boot_sector_t *bs = (fat_boot_sector_t *)sector_buffer;

    sectors_per_cluster = bs->sectors_per_cluster;
    sectors_per_fat = bs->sectors_per_fat;
    root_dir_entries = bs->dir_entry_count;

    fat_start_lba = bs->reserved_sectors;
    root_dir_lba = fat_start_lba + (bs->fat_count * sectors_per_fat);
    
    uint32_t root_dir_size_sectors = (root_dir_entries * 32 + 511) / 512;
    data_start_lba = root_dir_lba + root_dir_size_sectors;
}

uint16_t fat_read_entry(uint16_t cluster) {
    uint32_t fat_offset = cluster * 2;
    uint32_t fat_sector = fat_start_lba + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    ata_read_sector(fat_sector, sector_buffer);
    uint16_t *table_value = (uint16_t*)&sector_buffer[ent_offset];
    return *table_value;
}

uint8_t* fat_read_file(const char *filename_8_3, uint32_t *out_size) {
    ata_read_sector(root_dir_lba, sector_buffer);
    fat_dir_entry_t *entry = (fat_dir_entry_t *)sector_buffer;

    int found = -1;
    for (int i = 0; i < 16; i++) {
        if (entry[i].filename[0] == 0) break;
        
        if (memcmp(entry[i].filename, filename_8_3, 11) == 0) {
            found = i;
            break;
        }
    }

    if (found == -1) return 0;

    uint32_t size = entry[found].file_size;
    if (out_size) *out_size = size;
    
    uint8_t *buffer = (uint8_t*)kmalloc(size + 512); 
    if (!buffer) return 0;

    uint8_t *ptr = buffer;

    uint16_t cluster = entry[found].first_cluster_low;
    
    while (cluster < 0xFFF8) { 
        uint32_t cluster_lba = data_start_lba + (cluster - 2) * sectors_per_cluster;
        
        for (int i = 0; i < sectors_per_cluster; i++) {
            ata_read_sector(cluster_lba + i, sector_buffer);
            
            uint32_t bytes_left = size - (uint32_t)(ptr - buffer);
            uint32_t to_copy = (bytes_left > 512) ? 512 : bytes_left;
            
            if (to_copy > 0) {
                memcpy(ptr, sector_buffer, to_copy);
                ptr += to_copy;
            }
        }
        
        cluster = fat_read_entry(cluster);
    }

    return buffer;
}