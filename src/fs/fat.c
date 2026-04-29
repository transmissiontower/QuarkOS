// SPDX-License-Identifier: MIT
/*
 * src/fs/fat.c - FAT16/FAT32 filesystem driver with path-based file reading
 */
#include "fs/fat.h"
#include "drivers/ata.h"
#include "mm/kmalloc.h"
#include "util/util.h"

static uint16_t g_bytes_per_sector;
static uint8_t  g_sectors_per_cluster;
static uint16_t g_reserved_sectors;
static uint8_t  g_fat_count;
static uint16_t g_dir_entry_count;
static uint32_t g_sectors_per_fat;
static uint32_t g_fat_lba;
static uint32_t g_root_lba;
static uint32_t g_root_sectors;
static uint32_t g_data_lba;
static int      g_is_fat32;
static uint32_t g_root_cluster;

static uint8_t sector_buf[512];
static uint8_t fat_buf[512];

void fat_init(void)
{
    drv_stor_ata_read(0, sector_buf);
    fat_boot_sector_t *bs = (fat_boot_sector_t *)sector_buf;
    g_bytes_per_sector    = bs->bytes_per_sector;
    g_sectors_per_cluster = bs->sectors_per_cluster;
    g_reserved_sectors    = bs->reserved_sectors;
    g_fat_count           = bs->fat_count;
    g_dir_entry_count     = bs->dir_entry_count;

    g_is_fat32 = (bs->sectors_per_fat == 0);
    if (g_is_fat32) {
        g_sectors_per_fat = FAT32_SECTORS_PER_FAT(sector_buf);
        g_root_cluster    = FAT32_ROOT_CLUSTER(sector_buf);
    } else {
        g_sectors_per_fat = bs->sectors_per_fat;
        g_root_cluster    = 0;
    }

    g_fat_lba    = g_reserved_sectors;
    g_root_lba   = g_fat_lba + (uint32_t)g_fat_count * g_sectors_per_fat;
    g_root_sectors = ((uint32_t)g_dir_entry_count * 32 + 511) / 512;
    g_data_lba   = g_root_lba + g_root_sectors;
}

void fat_list_root(void) {}

static uint32_t fat_next_cluster(uint32_t cluster)
{
    if (g_is_fat32) {
        uint32_t offset = cluster * 4;
        uint32_t sector = g_fat_lba + offset / 512;
        uint32_t off_in = offset % 512;
        drv_stor_ata_read(sector, fat_buf);
        return *(uint32_t *)(fat_buf + off_in) & 0x0FFFFFFFu;
    } else {
        uint32_t offset = (uint32_t)cluster * 2;
        uint32_t sector = g_fat_lba + offset / 512;
        uint32_t off_in = offset % 512;
        drv_stor_ata_read(sector, fat_buf);
        return *(uint16_t *)(fat_buf + off_in);
    }
}

#define FAT_EOC  (g_is_fat32 ? 0x0FFFFFF8u : 0xFFF8u)

static inline uint32_t dir_entry_cluster(const fat_dir_entry_t *e)
{
    if (g_is_fat32)
        return ((uint32_t)e->first_cluster_high << 16) | e->first_cluster_low;
    return e->first_cluster_low;
}

uint8_t *fat_read_file(const char *filename_8_3, uint32_t *out_size)
{
    uint32_t first_cluster = 0;
    uint32_t file_size = 0;

    if (g_is_fat32) {

        uint32_t rc = g_root_cluster;
        while (rc < FAT_EOC) {
            uint32_t lba = g_data_lba + (rc - 2) * g_sectors_per_cluster;
            for (uint8_t s = 0; s < g_sectors_per_cluster; s++) {
                drv_stor_ata_read(lba + s, sector_buf);
                fat_dir_entry_t *dir = (fat_dir_entry_t *)sector_buf;
                for (int i = 0; i < 16; i++) {
                    if ((uint8_t)dir[i].filename[0] == 0x00) goto done;
                    if ((uint8_t)dir[i].filename[0] == 0xE5) continue;
                    if (dir[i].attributes & 0x08) continue;
                    if (dir[i].attributes & 0x10) continue;
                    int match = 1;
                    for (int j = 0; j < 8 && match; j++)
                        if (dir[i].filename[j] != filename_8_3[j]) match = 0;
                    for (int j = 0; j < 3 && match; j++)
                        if (dir[i].ext[j] != filename_8_3[8+j]) match = 0;
                    if (match) {
                        first_cluster = dir_entry_cluster(&dir[i]);
                        file_size = dir[i].file_size;
                        goto done;
                    }
                }
            }
            rc = fat_next_cluster(rc);
        }
    } else {

        for (uint32_t s = 0; s < g_root_sectors; s++) {
            drv_stor_ata_read(g_root_lba + s, sector_buf);
            fat_dir_entry_t *dir = (fat_dir_entry_t *)sector_buf;
            for (int i = 0; i < 16; i++) {
                if ((uint8_t)dir[i].filename[0] == 0x00) goto done;
                if ((uint8_t)dir[i].filename[0] == 0xE5) continue;
                if (dir[i].attributes & 0x08) continue;
                if (dir[i].attributes & 0x10) continue;
                int match = 1;
                for (int j = 0; j < 8 && match; j++)
                    if (dir[i].filename[j] != filename_8_3[j]) match = 0;
                for (int j = 0; j < 3 && match; j++)
                    if (dir[i].ext[j] != filename_8_3[8+j]) match = 0;
                if (match) {
                    first_cluster = dir[i].first_cluster_low;
                    file_size = dir[i].file_size;
                    goto done;
                }
            }
        }
    }
done:
    if (!first_cluster || !file_size)
        return 0;

    uint8_t *buf = (uint8_t *)mm_heap_alloc(file_size);
    if (!buf)
        return 0;

    uint32_t copied  = 0;
    uint32_t cluster = first_cluster;
    while (cluster < FAT_EOC && copied < file_size) {
        uint32_t lba = g_data_lba + (cluster - 2) * g_sectors_per_cluster;
        for (uint8_t s = 0; s < g_sectors_per_cluster && copied < file_size; s++) {
            drv_stor_ata_read(lba + s, sector_buf);
            uint32_t to_copy = file_size - copied;
            if (to_copy > 512)
                to_copy = 512;
            memcpy(buf + copied, sector_buf, to_copy);
            copied += to_copy;
        }
        cluster = fat_next_cluster(cluster);
    }

    *out_size = file_size;
    return buf;
}

static uint32_t fat_find_in_dir(uint32_t cluster, const char *name11,
                                int *is_dir, uint32_t *file_size_out)
{
    for (;;) {
        uint32_t lba, sectors;
        if (!g_is_fat32 && cluster == 0) {
            lba     = g_root_lba;
            sectors = g_root_sectors;
        } else {
            lba     = g_data_lba + (cluster - 2) * g_sectors_per_cluster;
            sectors = g_sectors_per_cluster;
        }
        for (uint32_t s = 0; s < sectors; s++) {
            drv_stor_ata_read(lba + s, sector_buf);
            fat_dir_entry_t *dir = (fat_dir_entry_t *)sector_buf;
            for (int i = 0; i < 16; i++) {
                if ((uint8_t)dir[i].filename[0] == 0x00) return 0;
                if ((uint8_t)dir[i].filename[0] == 0xE5) continue;
                if (dir[i].attributes & 0x08) continue;
                int match = 1;
                for (int j = 0; j < 8 && match; j++)
                    if (dir[i].filename[j] != name11[j]) match = 0;
                for (int j = 0; j < 3 && match; j++)
                    if (dir[i].ext[j] != name11[8+j]) match = 0;
                if (match) {
                    *is_dir = (dir[i].attributes & 0x10) ? 1 : 0;
                    if (file_size_out) *file_size_out = dir[i].file_size;
                    return dir_entry_cluster(&dir[i]);
                }
            }
        }
        if (!g_is_fat32 && cluster == 0) break;
        cluster = fat_next_cluster(cluster);
        if (cluster >= FAT_EOC) break;
    }
    return 0;
}

static void to_fat83(const char *seg, int len, char out[11])
{
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int ni = 0, ei = 0;
    int in_ext = 0;
    for (int i = 0; i < len; i++) {
        char c = seg[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c == '.') { in_ext = 1; continue; }
        if (!in_ext && ni < 8) out[ni++] = c;
        else if (in_ext && ei < 3) out[8 + ei++] = c;
    }
}

uint8_t *fat_read_file_path(const char *path, uint32_t *out_size)
{
    if (!path || path[0] != '/') return 0;
    const char *p = path + 1;
    uint32_t cluster = g_is_fat32 ? g_root_cluster : 0;
    while (*p) {
        const char *start = p;
        while (*p && *p != '/') p++;
        int seglen = (int)(p - start);
        if (*p == '/') p++;
        if (seglen == 0) continue;
        char name11[11];
        to_fat83(start, seglen, name11);
        int is_dir = 0;
        uint32_t fsz = 0;
        uint32_t next = fat_find_in_dir(cluster, name11, &is_dir, &fsz);
        if (!next) return 0;
        if (*p == 0) {
            if (!fsz) return 0;
            uint8_t *buf = (uint8_t *)mm_heap_alloc(fsz);
            if (!buf) return 0;
            uint32_t copied = 0;
            uint32_t cl = next;
            while (cl < FAT_EOC && copied < fsz) {
                uint32_t lba = g_data_lba + (cl - 2) * g_sectors_per_cluster;
                for (uint8_t s = 0; s < g_sectors_per_cluster && copied < fsz; s++) {
                    drv_stor_ata_read(lba + s, sector_buf);
                    uint32_t tc = fsz - copied;
                    if (tc > 512) tc = 512;
                    memcpy(buf + copied, sector_buf, tc);
                    copied += tc;
                }
                cl = fat_next_cluster(cl);
            }
            *out_size = fsz;
            return buf;
        }
        cluster = next;
    }
    return 0;
}
