#ifndef FAT_H
#define FAT_H

#include "lib/stdint.h"

typedef struct{
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t dir_entry_count;
    uint16_t total_sectors_small;
    uint8_t  media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_large;
    uint8_t  drive_number;
    uint8_t  reserved;
    uint8_t  signature;
    uint32_t vol_id;
    char     vol_label[11];
    char     fs_type[8];
} __attribute__((packed)) fat_boot_sector_t;

typedef struct{
    char     filename[8];
    char     ext[3];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high; 
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat_dir_entry_t;

void fat_init();
void fat_list_root();
uint8_t* fat_read_file(const char *filename, uint32_t *out_size);

#endif