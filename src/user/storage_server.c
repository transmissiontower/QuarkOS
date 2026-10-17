// SPDX-License-Identifier: MIT
#include "fs/fat.h"
#include "kernel/ipc.h"
#include "lib/stdbool.h"
#include "lib/stdlib.h"
#include "user/lib/libapi.h"
typedef struct {
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t dir_entry_count;
    uint32_t sectors_per_fat;
    uint32_t bytes_per_cluster;
} fat_info_t;
static fat_info_t fat;
static uint32_t fat_start_lba;
static uint32_t root_dir_lba;
static uint32_t root_sectors;
static uint32_t data_start_lba;
static uint8_t *current_file_data = 0;
static uint32_t current_file_size = 0;
#define SHARED_MAX_PAGES 4096
static uint32_t current_file_pages[SHARED_MAX_PAGES];
static uint32_t current_file_page_count = 0;
static uint32_t current_file_page_offset = 0;
static uint32_t fat_cached_sector = 0xFFFFFFFFu;
static uint8_t fat_cache_buf[512];
static int fat_cache_dirty = 0;
static uint32_t fat_next_free_hint = 2;
static int current_disk_idx = 0;
static uint32_t current_partition_lba = 0;
static int is_fat32 = 0;
static uint32_t root_cluster = 0;
#define MAX_DISKS 8
static int disk_mounted[MAX_DISKS] = {1, 1, 1, 1, 1, 1, 1, 1};
#define FAT_EOC_VAL (is_fat32 ? 0x0FFFFFFFu : 0xFFFFu)
#define FAT_IS_EOC(c) (is_fat32 ? ((c) >= 0x0FFFFFF8u) : ((c) >= 0xFFF8u))
#define FAT_BAD_CLUSTER 0xFFFFFFFFu
static inline uint32_t entry_cluster(const fat_dir_entry_t *e) {
    if (is_fat32)
        return ((uint32_t)e->first_cluster_high << 16) | e->first_cluster_low;
    return e->first_cluster_low;
}
typedef struct {
    int active;
    int sender_pid;
    uint32_t dir_cluster;
    uint32_t expected_size;
    uint32_t received_size;
    char filename[64];
    uint8_t *buffer;
} save_session_t;
static save_session_t save_session;
static void reset_save_session(void) {
    if (save_session.buffer) {
        free(save_session.buffer);
    }
    memset(&save_session, 0, sizeof(save_session));
}
static int virtio_read_sectors(uint32_t lba, uint32_t count, uint8_t *buffer) {
    uint32_t done = 0;
    while (done < count) {
        uint32_t chunk = count - done;
        if (chunk > 128)
            chunk = 128;
        int r = qk_blk_read(current_partition_lba + lba + done, chunk, buffer + done * 512);
        if (r != 0)
            return r;
        done += chunk;
    }
    return 0;
}
int storage_read_sector(uint32_t lba, uint8_t *buffer) {
    return qk_blk_read(current_partition_lba + lba, 1, buffer);
}
int storage_write_sector(uint32_t lba, uint8_t *buffer) {
    return qk_blk_write(current_partition_lba + lba, 1, buffer);
}
static int storage_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buffer) {
    if (count == 0)
        return 0;
    return qk_blk_write(current_partition_lba + lba, count, buffer);
}

static int valid_fat_boot_sector(const uint8_t *sector) {
    uint16_t bytes_per_sector = (uint16_t)sector[11] | ((uint16_t)sector[12] << 8);
    uint8_t sectors_per_cluster = sector[13];
    uint16_t reserved_sectors = (uint16_t)sector[14] | ((uint16_t)sector[15] << 8);
    uint8_t fat_count = sector[16];

    return bytes_per_sector == 512 && sectors_per_cluster &&
           !(sectors_per_cluster & (sectors_per_cluster - 1)) &&
           sectors_per_cluster <= 128 && reserved_sectors && fat_count;
}

static int read_current_boot_sector(uint8_t *sector) {
    current_partition_lba = 0;
    if (qk_blk_read(0, 1, sector) != 0)
        return -1;
    if (valid_fat_boot_sector(sector) || sector[510] != 0x55 || sector[511] != 0xAA)
        return 0;

    for (int i = 0; i < 4; i++) {
        const uint8_t *entry = sector + 446 + i * 16;
        uint8_t type = entry[4];
        uint32_t start_lba = (uint32_t)entry[8] | ((uint32_t)entry[9] << 8) |
                             ((uint32_t)entry[10] << 16) | ((uint32_t)entry[11] << 24);
        if ((type == 0x0B || type == 0x0C || type == 0x1B || type == 0x1C) && start_lba) {
            current_partition_lba = start_lba;
            if (qk_blk_read(start_lba, 1, sector) == 0 && valid_fat_boot_sector(sector))
                return 0;
            current_partition_lba = 0;
            qk_blk_read(0, 1, sector);
            return 0;
        }
    }
    return 0;
}

static int fat_find_entry_in_dir(uint32_t dir_cluster, const char *fat_name,
                                 fat_dir_entry_t *out_entry);

static uint8_t lfn_checksum(const char *short_name_11) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + (uint8_t)short_name_11[i];
    return sum;
}

static void lfn_extract_chars(const fat_lfn_entry_t *lfn, int seq_index, char *out_name,
                              int max_len) {
    int base = seq_index * 13;
    const uint8_t *parts[] = {lfn->name1, lfn->name2, lfn->name3};
    const int lens[] = {5, 6, 2};
    int pos = base;
    for (int p = 0; p < 3; p++) {
        for (int i = 0; i < lens[p]; i++) {
            uint16_t ch = parts[p][i * 2] | (parts[p][i * 2 + 1] << 8);
            if (pos < max_len - 1) {
                if (ch == 0x0000 || ch == 0xFFFF)
                    out_name[pos] = 0;
                else
                    out_name[pos] = (char)(ch & 0x7F);
            }
            pos++;
        }
    }
}

static void lfn_build_entry(fat_lfn_entry_t *lfn, int seq_num, int is_last, const char *long_name,
                            int name_len, uint8_t chksum) {
    memset(lfn, 0xFF, 32);
    lfn->sequence = (uint8_t)seq_num;
    if (is_last)
        lfn->sequence |= 0x40;
    lfn->attributes = 0x0F;
    lfn->type = 0x00;
    lfn->checksum = chksum;
    lfn->first_cluster = 0x0000;

    int base = (seq_num - 1) * 13;
    uint8_t *parts[] = {lfn->name1, lfn->name2, lfn->name3};
    const int lens[] = {5, 6, 2};
    int pos = base;
    for (int p = 0; p < 3; p++) {
        for (int i = 0; i < lens[p]; i++) {
            uint16_t ch;
            if (pos < name_len)
                ch = (uint8_t)long_name[pos];
            else if (pos == name_len)
                ch = 0x0000;
            else
                ch = 0xFFFF;
            parts[p][i * 2] = (uint8_t)(ch & 0xFF);
            parts[p][i * 2 + 1] = (uint8_t)(ch >> 8);
            pos++;
        }
    }
}

static int needs_lfn(const char *name) {
    int len = strlen(name);
    int dot_pos = -1;
    for (int i = 0; i < len; i++) {
        if (name[i] == '.')
            dot_pos = i;
        if (name[i] >= 'a' && name[i] <= 'z')
            return 1;
        if (name[i] == ' ')
            return 1;
    }
    int base_len = (dot_pos >= 0) ? dot_pos : len;
    int ext_len = (dot_pos >= 0) ? len - dot_pos - 1 : 0;
    if (base_len > 8 || ext_len > 3)
        return 1;
    return 0;
}

static void generate_short_name(const char *long_name, uint32_t dir_cluster, char *out_11) {
    memset(out_11, ' ', 11);
    int len = strlen(long_name);
    int dot_pos = -1;
    for (int i = len - 1; i >= 0; i--)
        if (long_name[i] == '.') {
            dot_pos = i;
            break;
        }

    int base_end = (dot_pos >= 0) ? dot_pos : len;
    int bi = 0;
    for (int i = 0; i < base_end && bi < 6; i++) {
        char c = long_name[i];
        if (c >= 'a' && c <= 'z')
            c -= 32;
        if (c == ' ' || c == '+' || c == ',' || c == ';' || c == '=' || c == '[' || c == ']')
            c = '_';
        if (c != '.')
            out_11[bi++] = c;
    }

    if (dot_pos >= 0) {
        int ei = 0;
        for (int i = dot_pos + 1; i < len && ei < 3; i++) {
            char c = long_name[i];
            if (c >= 'a' && c <= 'z')
                c -= 32;
            out_11[8 + ei++] = c;
        }
    }

    for (int n = 1; n <= 9; n++) {
        out_11[6] = '~';
        out_11[7] = '0' + n;
        fat_dir_entry_t tmp;
        if (fat_find_entry_in_dir(dir_cluster, out_11, &tmp) != 0)
            return;
    }
}

void to_fat_name(const char *src, char *dest) {
    memset(dest, ' ', 11);
    int i = 0, j = 0;
    while (src[i] && src[i] != '.') {
        if (j < 8) {
            char c = src[i];
            if (c >= 'a' && c <= 'z')
                c -= 32;
            dest[j++] = c;
        }
        i++;
    }
    if (src[i] == '.') {
        i++;
        j = 8;
        while (src[i]) {
            if (j < 11) {
                char c = src[i];
                if (c >= 'a' && c <= 'z')
                    c -= 32;
                dest[j++] = c;
            }
            i++;
        }
    }
}
static void fat_cache_flush(void) {
    if (fat_cache_dirty && fat_cached_sector != 0xFFFFFFFFu) {
        storage_write_sector(fat_cached_sector, fat_cache_buf);
        fat_cache_dirty = 0;
    }
}
static void fat_cache_load(uint32_t sector) {
    if (fat_cached_sector != sector) {
        fat_cache_flush();
        storage_read_sector(sector, fat_cache_buf);
        fat_cached_sector = sector;
    }
}
uint32_t fat_get_entry(uint32_t cluster) {
    if (is_fat32) {
        uint32_t byte_off = cluster * 4;
        uint32_t sector = fat_start_lba + (byte_off / fat.bytes_per_sector);
        fat_cache_load(sector);
        uint32_t offset = byte_off % fat.bytes_per_sector;
        return *(uint32_t *)&fat_cache_buf[offset] & 0x0FFFFFFFu;
    }
    uint32_t sector = fat_start_lba + ((cluster * 2) / fat.bytes_per_sector);
    fat_cache_load(sector);
    uint32_t offset = (cluster * 2) % fat.bytes_per_sector;
    return *(uint16_t *)&fat_cache_buf[offset];
}
uint8_t *fat_read_file_server(uint32_t cluster, uint32_t size) {
    if (size == 0)
        return 0;
    uint8_t *data = malloc(size + fat.bytes_per_cluster + 512);
    if (!data)
        return 0;
    uint32_t alloc_size = size + fat.bytes_per_cluster + 512;
    for (uint32_t i = 0; i < alloc_size; i += 4096)
        ((volatile uint8_t *)data)[i] = 0;
    uint8_t *ptr = data;
    uint32_t cur = cluster;
    uint32_t total_read = 0;
    while (!FAT_IS_EOC(cur) && cur >= 2 && total_read < size) {
        uint32_t run_start = cur;
        uint32_t run_clusters = 1;
        uint32_t next = fat_get_entry(cur);
        while (next == cur + 1 && !FAT_IS_EOC(next) && next >= 2) {
            run_clusters++;
            cur = next;
            next = fat_get_entry(cur);
        }
        cur = next;
        uint32_t lba = data_start_lba + (run_start - 2) * fat.sectors_per_cluster;
        uint32_t sectors = run_clusters * fat.sectors_per_cluster;
        uint8_t *rptr = ptr;
        while (sectors > 0) {
            uint32_t batch = (sectors > 255) ? 255 : sectors;
            virtio_read_sectors(lba, batch, rptr);
            lba += batch;
            rptr += batch * 512;
            sectors -= batch;
        }
        uint32_t run_bytes = run_clusters * fat.bytes_per_cluster;
        uint32_t left = size - total_read;
        uint32_t chunk = (left < run_bytes) ? left : run_bytes;
        ptr += chunk;
        total_read += chunk;
    }
    return data;
}
#define DIR_BULK_MAX_SECTORS 32
static uint8_t dir_bulk_buf[DIR_BULK_MAX_SECTORS * 512];
static int fat_find_entry_in_dir(uint32_t dir_cluster, const char *fat_name,
                                 fat_dir_entry_t *out_entry) {
    if (is_fat32 && dir_cluster == 0)
        dir_cluster = root_cluster;
    uint32_t lba = (!is_fat32 && dir_cluster == 0)
                       ? root_dir_lba
                       : (data_start_lba + (dir_cluster - 2) * fat.sectors_per_cluster);
    uint32_t scnt = (!is_fat32 && dir_cluster == 0) ? root_sectors : fat.sectors_per_cluster;
    if (scnt > DIR_BULK_MAX_SECTORS)
        scnt = DIR_BULK_MAX_SECTORS;
    virtio_read_sectors(lba, scnt, dir_bulk_buf);
    fat_dir_entry_t *e = (fat_dir_entry_t *)dir_bulk_buf;
    uint32_t total_entries = scnt * 16;
    for (uint32_t i = 0; i < total_entries; i++) {
        if ((uint8_t)e[i].filename[0] == 0)
            return -1;
        if ((uint8_t)e[i].filename[0] == 0xE5)
            continue;
        if (memcmp(e[i].filename, fat_name, 11) == 0) {
            if (out_entry)
                memcpy(out_entry, &e[i], sizeof(fat_dir_entry_t));
            return 0;
        }
    }
    return -1;
}

static int fat_find_entry_lfn(uint32_t dir_cluster, const char *name, fat_dir_entry_t *out_entry) {
    if (is_fat32 && dir_cluster == 0)
        dir_cluster = root_cluster;
    int fat16_root = (!is_fat32 && dir_cluster == 0);
    char lfn_buf[64];
    int lfn_valid = 0;
    memset(lfn_buf, 0, 64);
    uint32_t cur_cluster = dir_cluster;
    int done = 0;
    while (!done) {
        uint32_t lba = fat16_root ? root_dir_lba
                                  : (data_start_lba + (cur_cluster - 2) * fat.sectors_per_cluster);
        uint32_t scnt = fat16_root ? root_sectors : fat.sectors_per_cluster;
        if (scnt > DIR_BULK_MAX_SECTORS)
            scnt = DIR_BULK_MAX_SECTORS;
        virtio_read_sectors(lba, scnt, dir_bulk_buf);
        fat_dir_entry_t *e = (fat_dir_entry_t *)dir_bulk_buf;
        uint32_t total_entries = scnt * 16;

        for (uint32_t i = 0; i < total_entries; i++) {
            uint8_t fc = (uint8_t)e[i].filename[0];
            if (fc == 0)
                return -1;
            if (fc == 0xE5) {
                lfn_valid = 0;
                memset(lfn_buf, 0, 64);
                continue;
            }
            if ((e[i].attributes & 0x0F) == 0x0F) {
                fat_lfn_entry_t *lfn = (fat_lfn_entry_t *)&e[i];
                int seq = lfn->sequence & 0x3F;
                if (lfn->sequence & 0x40) {
                    memset(lfn_buf, 0, 64);
                    lfn_valid = 1;
                }
                if (lfn_valid && seq >= 1 && seq <= 4)
                    lfn_extract_chars(lfn, seq - 1, lfn_buf, 64);
                continue;
            }

            if (lfn_valid && lfn_buf[0]) {
                int match = 1;
                const char *a = name, *b = lfn_buf;
                while (*a && *b) {
                    char ca = *a, cb = *b;
                    if (ca >= 'a' && ca <= 'z')
                        ca -= 32;
                    if (cb >= 'a' && cb <= 'z')
                        cb -= 32;
                    if (ca != cb) {
                        match = 0;
                        break;
                    }
                    a++;
                    b++;
                }
                if (*a || *b)
                    match = 0;
                if (match) {
                    if (out_entry)
                        memcpy(out_entry, &e[i], sizeof(fat_dir_entry_t));
                    return 0;
                }
            }
            lfn_valid = 0;
            memset(lfn_buf, 0, 64);

            char fat_n[11];
            to_fat_name(name, fat_n);
            if (memcmp(e[i].filename, fat_n, 11) == 0) {
                if (out_entry)
                    memcpy(out_entry, &e[i], sizeof(fat_dir_entry_t));
                return 0;
            }
        }

        if (!fat16_root) {
            uint32_t next = fat_get_entry(cur_cluster);
            uint32_t eof = is_fat32 ? 0x0FFFFFF8u : 0xFFF8u;
            if (next >= eof)
                done = 1;
            else
                cur_cluster = next;
        } else {
            done = 1;
        }
    }
    return -1;
}

static int fat_resolve_path(const char *path, uint32_t *out_dir_cluster, char *out_fat_name) {
    uint32_t cluster = 0;
    const char *p = path;
    if (*p == '/')
        p++;
    char seg[64];
    while (*p) {
        int si = 0;
        while (*p && *p != '/') {
            if (si < 63)
                seg[si++] = *p;
            p++;
        }
        seg[si] = 0;
        if (*p == '/') {
            fat_dir_entry_t entry;
            if (fat_find_entry_lfn(cluster, seg, &entry) != 0)
                return -1;
            if (!(entry.attributes & 0x10))
                return -1;
            cluster = entry_cluster(&entry);
            p++;
        } else {
            if (out_dir_cluster)
                *out_dir_cluster = cluster;
            if (out_fat_name)
                to_fat_name(seg, out_fat_name);
            return 0;
        }
    }
    return -1;
}
static uint32_t cluster_to_lba(uint32_t cluster) {
    if (cluster < 2)
        return data_start_lba;
    return data_start_lba + (cluster - 2) * fat.sectors_per_cluster;
}
static void fat_set_entry(uint32_t cluster, uint32_t value) {
    if (is_fat32) {
        uint32_t byte_off = cluster * 4;
        uint32_t sector = fat_start_lba + (byte_off / fat.bytes_per_sector);
        fat_cache_load(sector);
        uint32_t offset = byte_off % fat.bytes_per_sector;
        uint32_t old = *(uint32_t *)&fat_cache_buf[offset];
        *(uint32_t *)&fat_cache_buf[offset] = (old & 0xF0000000u) | (value & 0x0FFFFFFFu);
        fat_cache_dirty = 1;
        return;
    }
    uint32_t sector = fat_start_lba + ((cluster * 2) / fat.bytes_per_sector);
    fat_cache_load(sector);
    uint32_t offset = (cluster * 2) % fat.bytes_per_sector;
    *(uint16_t *)&fat_cache_buf[offset] = (uint16_t)value;
    fat_cache_dirty = 1;
}
static uint32_t fat_find_free_cluster(void) {
    uint32_t entry_size = is_fat32 ? 4u : 2u;
    uint32_t total_entries = (fat.sectors_per_fat * fat.bytes_per_sector) / entry_size;
    if (total_entries < 2)
        return FAT_BAD_CLUSTER;
    uint32_t start = fat_next_free_hint;
    if (start < 2)
        start = 2;
    for (uint32_t scanned = 0; scanned < total_entries; scanned++) {
        uint32_t cluster = start + scanned;
        if (cluster >= total_entries)
            cluster = 2 + (cluster - total_entries);
        if (cluster >= total_entries)
            cluster = 2;
        if (fat_get_entry(cluster) == 0) {
            fat_next_free_hint = cluster + 1;
            if (fat_next_free_hint >= total_entries)
                fat_next_free_hint = 2;
            return cluster;
        }
    }
    return FAT_BAD_CLUSTER;
}
static void fat_free_chain(uint32_t start_cluster) {
    uint32_t cur = start_cluster;
    while (cur >= 2 && !FAT_IS_EOC(cur)) {
        uint32_t next = fat_get_entry(cur);
        fat_set_entry(cur, 0);
        if (cur < fat_next_free_hint)
            fat_next_free_hint = cur;
        cur = next;
    }
    fat_cache_flush();
}
static int fat_allocate_chain(uint32_t size, uint32_t *out_first_cluster) {
    if (!out_first_cluster)
        return -1;
    uint32_t clusters_needed = (size + fat.bytes_per_cluster - 1) / fat.bytes_per_cluster;
    if (clusters_needed == 0)
        clusters_needed = 1;
    uint32_t first = 0, prev = 0;
    for (uint32_t i = 0; i < clusters_needed; i++) {
        uint32_t cluster = fat_find_free_cluster();
        if (cluster == FAT_BAD_CLUSTER) {
            if (first >= 2)
                fat_free_chain(first);
            return -1;
        }
        fat_set_entry(cluster, FAT_EOC_VAL);
        if (prev >= 2)
            fat_set_entry(prev, cluster);
        else
            first = cluster;
        prev = cluster;
    }
    fat_set_entry(prev, FAT_EOC_VAL);
    fat_cache_flush();
    *out_first_cluster = first;
    return 0;
}
static void fat_zero_cluster(uint32_t cluster) {
    uint32_t lba = cluster_to_lba(cluster);
    uint32_t spc = fat.sectors_per_cluster;
    uint8_t *buf = malloc(spc * 512);
    if (buf) {
        memset(buf, 0, spc * 512);
        storage_write_sectors(lba, (uint8_t)spc, buf);
        free(buf);
    } else {
        uint8_t zero[512];
        memset(zero, 0, sizeof(zero));
        for (uint32_t i = 0; i < spc; i++)
            storage_write_sector(lba + i, zero);
    }
}
static void fat_write_chain(uint32_t start_cluster, const uint8_t *data, uint32_t size) {
    if (start_cluster < 2)
        return;
    uint32_t bytes_per_cluster = fat.bytes_per_cluster;
    uint32_t spc = fat.sectors_per_cluster;
    uint8_t *cbuf = malloc(bytes_per_cluster);
    if (!cbuf)
        return;
    uint32_t cur = start_cluster;
    uint32_t remaining = size;
    while (cur >= 2 && !FAT_IS_EOC(cur)) {
        uint32_t lba = cluster_to_lba(cur);
        uint32_t to_copy = (remaining > bytes_per_cluster) ? bytes_per_cluster : remaining;
        if (to_copy > 0 && data)
            memcpy(cbuf, data, to_copy);
        if (to_copy < bytes_per_cluster)
            memset(cbuf + to_copy, 0, bytes_per_cluster - to_copy);
        storage_write_sectors(lba, (uint8_t)spc, cbuf);
        if (remaining <= bytes_per_cluster) {
            remaining = 0;
        } else {
            remaining -= bytes_per_cluster;
            if (data)
                data += bytes_per_cluster;
        }
        if (remaining == 0)
            break;
        cur = fat_get_entry(cur);
    }
    free(cbuf);
}
static uint32_t dir_first_lba(uint32_t dir_cluster) {
    if (is_fat32 && dir_cluster == 0)
        dir_cluster = root_cluster;
    return (!is_fat32 && dir_cluster == 0) ? root_dir_lba : cluster_to_lba(dir_cluster);
}
static uint32_t dir_sector_count(uint32_t dir_cluster) {
    if (is_fat32 && dir_cluster == 0)
        dir_cluster = root_cluster;
    return (!is_fat32 && dir_cluster == 0) ? root_sectors : fat.sectors_per_cluster;
}
static int fat_lookup_entry_with_location(uint32_t dir_cluster, const char *fat_name,
                                          fat_dir_entry_t *out_entry, uint32_t *out_lba,
                                          int *out_index) {
    uint32_t lba = dir_first_lba(dir_cluster);
    uint32_t scnt = dir_sector_count(dir_cluster);
    if (scnt > DIR_BULK_MAX_SECTORS)
        scnt = DIR_BULK_MAX_SECTORS;
    virtio_read_sectors(lba, scnt, dir_bulk_buf);
    fat_dir_entry_t *entries = (fat_dir_entry_t *)dir_bulk_buf;
    uint32_t total_entries = scnt * 16;
    for (uint32_t i = 0; i < total_entries; i++) {
        uint8_t first_char = (uint8_t)entries[i].filename[0];
        if (first_char == 0)
            return -1;
        if (first_char == 0xE5)
            continue;
        if (memcmp(entries[i].filename, fat_name, 11) == 0) {
            if (out_entry)
                memcpy(out_entry, &entries[i], sizeof(fat_dir_entry_t));
            if (out_lba)
                *out_lba = lba + (i / 16);
            if (out_index)
                *out_index = (int)(i % 16);
            return 0;
        }
    }
    return -1;
}
static int fat_find_free_entry_slot(uint32_t dir_cluster, uint32_t *out_lba, int *out_index) {
    uint32_t lba = dir_first_lba(dir_cluster);
    uint32_t scnt = dir_sector_count(dir_cluster);
    if (scnt > DIR_BULK_MAX_SECTORS)
        scnt = DIR_BULK_MAX_SECTORS;
    virtio_read_sectors(lba, scnt, dir_bulk_buf);
    fat_dir_entry_t *entries = (fat_dir_entry_t *)dir_bulk_buf;
    uint32_t total_entries = scnt * 16;
    for (uint32_t i = 0; i < total_entries; i++) {
        uint8_t first_char = (uint8_t)entries[i].filename[0];
        if (first_char == 0 || first_char == 0xE5) {
            if (out_lba)
                *out_lba = lba + (i / 16);
            if (out_index)
                *out_index = (int)(i % 16);
            return 0;
        }
    }
    return -1;
}

static int fat_find_free_entry_slots(uint32_t dir_cluster, int count, uint32_t *out_lba,
                                     int *out_index) {
    if (count <= 1)
        return fat_find_free_entry_slot(dir_cluster, out_lba, out_index);
    uint32_t lba = dir_first_lba(dir_cluster);
    uint32_t scnt = dir_sector_count(dir_cluster);
    if (scnt > DIR_BULK_MAX_SECTORS)
        scnt = DIR_BULK_MAX_SECTORS;
    virtio_read_sectors(lba, scnt, dir_bulk_buf);
    fat_dir_entry_t *entries = (fat_dir_entry_t *)dir_bulk_buf;
    uint32_t total = scnt * 16;
    int run = 0, run_start = 0;
    for (uint32_t i = 0; i < total; i++) {
        uint8_t fc = (uint8_t)entries[i].filename[0];
        if (fc == 0 || fc == 0xE5) {
            if (run == 0)
                run_start = (int)i;
            run++;
            if (run >= count) {
                if (out_lba)
                    *out_lba = lba + (run_start / 16);
                if (out_index)
                    *out_index = run_start % 16;
                return 0;
            }
        } else {
            run = 0;
        }
    }
    return -1;
}

static int fat_write_entry_at(uint32_t lba, int entry_index, const fat_dir_entry_t *entry) {
    uint8_t buf[512];
    storage_read_sector(lba, buf);
    fat_dir_entry_t *entries = (fat_dir_entry_t *)buf;
    entries[entry_index] = *entry;
    storage_write_sector(lba, buf);
    return 0;
}
static void fat_fill_entry_name(fat_dir_entry_t *entry, const char *fat_name) {
    memcpy(entry->filename, fat_name, 8);
    memcpy(entry->ext, fat_name + 8, 3);
}
static int fat_save_file_to_dir(uint32_t dir_cluster, const char *filename, const uint8_t *data,
                                uint32_t size) {
    int use_lfn = needs_lfn(filename);
    char fat_name[11];
    if (use_lfn)
        generate_short_name(filename, dir_cluster, fat_name);
    else
        to_fat_name(filename, fat_name);

    fat_dir_entry_t existing;
    uint32_t entry_lba = 0;
    int entry_index = -1;
    bool found = (fat_lookup_entry_with_location(dir_cluster, fat_name, &existing, &entry_lba,
                                                 &entry_index) == 0);

    if (!found && use_lfn)
        found = (fat_find_entry_lfn(dir_cluster, filename, &existing) == 0);
    if (found && (existing.attributes & 0x10))
        return -1;

    uint32_t new_first_cluster = 0;
    if (size > 0) {
        if (fat_allocate_chain(size, &new_first_cluster) != 0)
            return -1;
        fat_write_chain(new_first_cluster, data, size);
    }
    if (found && entry_cluster(&existing) >= 2)
        fat_free_chain(entry_cluster(&existing));

    fat_dir_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    fat_fill_entry_name(&entry, fat_name);
    entry.attributes = 0x20;
    entry.first_cluster_low = new_first_cluster & 0xFFFF;
    entry.first_cluster_high = (new_first_cluster >> 16) & 0xFFFF;
    entry.file_size = size;

    if (use_lfn) {
        int name_len = strlen(filename);
        int num_lfn = (name_len + 12) / 13;
        int total_slots = num_lfn + 1;
        uint32_t slot_lba = 0;
        int slot_index = 0;
        if (fat_find_free_entry_slots(dir_cluster, total_slots, &slot_lba, &slot_index) != 0)
            return -1;
        uint8_t chk = lfn_checksum(fat_name);

        uint32_t base_lba = dir_first_lba(dir_cluster);
        for (int s = 0; s < num_lfn; s++) {
            int seq = num_lfn - s;
            fat_lfn_entry_t lfn;
            lfn_build_entry(&lfn, seq, (s == 0) ? 1 : 0, filename, name_len, chk);
            int abs_idx = slot_index + s;
            uint32_t lba2 = base_lba + (abs_idx / 16);
            int idx2 = abs_idx % 16;
            fat_write_entry_at(lba2, idx2, (fat_dir_entry_t *)&lfn);
        }

        int abs_idx = slot_index + num_lfn;
        uint32_t lba2 = base_lba + (abs_idx / 16);
        int idx2 = abs_idx % 16;
        fat_write_entry_at(lba2, idx2, &entry);
    } else {
        if (!found) {
            if (fat_find_free_entry_slot(dir_cluster, &entry_lba, &entry_index) != 0)
                return -1;
        }
        fat_write_entry_at(entry_lba, entry_index, &entry);
    }
    return 0;
}
static void prepare_special_entry(fat_dir_entry_t *entry, const char *name, uint32_t cluster) {
    memset(entry, 0, sizeof(*entry));
    memset(entry->filename, ' ', 8);
    memset(entry->ext, ' ', 3);
    for (int i = 0; name[i] && i < 8; i++)
        entry->filename[i] = name[i];
    entry->attributes = 0x10;
    entry->first_cluster_low = cluster & 0xFFFF;
    entry->first_cluster_high = (cluster >> 16) & 0xFFFF;
}
static int fat_create_directory_entry(uint32_t parent_cluster, const char *name) {
    char fat_name[11];
    to_fat_name(name, fat_name);
    if (fat_lookup_entry_with_location(parent_cluster, fat_name, 0, 0, 0) == 0)
        return -1;
    uint32_t new_cluster = fat_find_free_cluster();
    if (new_cluster == FAT_BAD_CLUSTER)
        return -1;
    fat_set_entry(new_cluster, FAT_EOC_VAL);
    fat_zero_cluster(new_cluster);
    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));
    fat_dir_entry_t *entries = (fat_dir_entry_t *)buf;
    fat_dir_entry_t dot, dotdot;
    prepare_special_entry(&dot, ".", new_cluster);
    prepare_special_entry(&dotdot, "..", (parent_cluster == 0) ? 0 : parent_cluster);
    entries[0] = dot;
    entries[1] = dotdot;
    storage_write_sector(cluster_to_lba(new_cluster), buf);
    fat_dir_entry_t new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    fat_fill_entry_name(&new_entry, fat_name);
    new_entry.attributes = 0x10;
    new_entry.first_cluster_low = new_cluster & 0xFFFF;
    new_entry.first_cluster_high = (new_cluster >> 16) & 0xFFFF;
    uint32_t entry_lba;
    int entry_index;
    if (fat_find_free_entry_slot(parent_cluster, &entry_lba, &entry_index) != 0) {
        fat_free_chain(new_cluster);
        return -1;
    }
    fat_write_entry_at(entry_lba, entry_index, &new_entry);
    return 0;
}
static uint32_t fat_resolve_dir_cluster(const char *path) {
    uint32_t cluster = 0;
    const char *p = path;
    if (*p == '/')
        p++;
    if (!*p)
        return cluster;
    char seg[64];
    while (*p) {
        int si = 0;
        while (*p && *p != '/') {
            if (si < 63)
                seg[si++] = *p;
            p++;
        }
        seg[si] = 0;
        if (*p == '/')
            p++;
        if (!seg[0])
            continue;
        fat_dir_entry_t entry;
        if (fat_find_entry_lfn(cluster, seg, &entry) != 0)
            return FAT_BAD_CLUSTER;
        if (!(entry.attributes & 0x10))
            return FAT_BAD_CLUSTER;
        cluster = entry_cluster(&entry);
    }
    return cluster;
}
static int fat_move_file_in_dir(uint32_t src_dir_cluster, uint32_t dst_dir_cluster,
                                const char *filename) {
    if (src_dir_cluster == dst_dir_cluster)
        return -1;
    uint32_t src_lba = dir_first_lba(src_dir_cluster);
    uint32_t src_scnt = dir_sector_count(src_dir_cluster);
    if (src_scnt > DIR_BULK_MAX_SECTORS)
        src_scnt = DIR_BULK_MAX_SECTORS;
    virtio_read_sectors(src_lba, src_scnt, dir_bulk_buf);
    fat_dir_entry_t *entries = (fat_dir_entry_t *)dir_bulk_buf;
    uint32_t total = src_scnt * 16;
    char fat_name[11];
    to_fat_name(filename, fat_name);
    int found_idx = -1;
    int lfn_seq_start = -1;
    int in_lfn = 0;
    char lfn_buf[64];
    memset(lfn_buf, 0, 64);
    for (uint32_t i = 0; i < total; i++) {
        uint8_t fc = (uint8_t)entries[i].filename[0];
        if (fc == 0)
            break;
        if (fc == 0xE5) {
            in_lfn = 0;
            lfn_seq_start = -1;
            memset(lfn_buf, 0, 64);
            continue;
        }
        if ((entries[i].attributes & 0x0F) == 0x0F) {
            fat_lfn_entry_t *lfn = (fat_lfn_entry_t *)&entries[i];
            if (lfn->sequence & 0x40) {
                lfn_seq_start = (int)i;
                in_lfn = 1;
                memset(lfn_buf, 0, 64);
            }
            if (in_lfn) {
                int seq = lfn->sequence & 0x3F;
                if (seq >= 1 && seq <= 4)
                    lfn_extract_chars(lfn, seq - 1, lfn_buf, 64);
            }
            continue;
        }
        int is_match = 0;
        if (in_lfn && lfn_buf[0]) {
            const char *a = filename, *b = lfn_buf;
            int match = 1;
            while (*a && *b) {
                char ca = *a, cb = *b;
                if (ca >= 'a' && ca <= 'z')
                    ca -= 32;
                if (cb >= 'a' && cb <= 'z')
                    cb -= 32;
                if (ca != cb) {
                    match = 0;
                    break;
                }
                a++;
                b++;
            }
            if (!*a && !*b)
                is_match = match;
        }
        if (!is_match && memcmp(entries[i].filename, fat_name, 11) == 0)
            is_match = 1;
        if (is_match) {
            found_idx = (int)i;
            break;
        }
        in_lfn = 0;
        lfn_seq_start = -1;
        memset(lfn_buf, 0, 64);
    }
    if (found_idx < 0)
        return -1;
    int first_src = (lfn_seq_start >= 0 && in_lfn) ? lfn_seq_start : found_idx;
    int nc = found_idx - first_src + 1;
    if (nc > 8)
        nc = 8;
    fat_dir_entry_t copy_buf[8];
    for (int k = 0; k < nc; k++)
        copy_buf[k] = entries[first_src + k];
    int del_first = first_src;
    int del_last = found_idx;
    uint32_t dst_slot_lba;
    int dst_slot_idx;
    if (fat_find_free_entry_slots(dst_dir_cluster, nc, &dst_slot_lba, &dst_slot_idx) != 0)
        return -1;
    uint32_t dst_base_lba = dir_first_lba(dst_dir_cluster);
    for (int k = 0; k < nc; k++) {
        int abs_dst = dst_slot_idx + k;
        uint32_t dlba = dst_base_lba + (abs_dst / 16);
        int didx = abs_dst % 16;
        fat_write_entry_at(dlba, didx, &copy_buf[k]);
    }
    for (int k = del_first; k <= del_last; k++) {
        uint32_t slba = src_lba + ((uint32_t)k / 16);
        int sidx = k % 16;
        uint8_t buf[512];
        storage_read_sector(slba, buf);
        buf[sidx * 32] = 0xE5;
        storage_write_sector(slba, buf);
    }
    return 0;
}
static int fat_delete_file_in_dir(uint32_t dir_cluster, const char *filename) {
    uint32_t lba = dir_first_lba(dir_cluster);
    uint32_t scnt = dir_sector_count(dir_cluster);
    if (scnt > DIR_BULK_MAX_SECTORS)
        scnt = DIR_BULK_MAX_SECTORS;
    virtio_read_sectors(lba, scnt, dir_bulk_buf);
    fat_dir_entry_t *entries = (fat_dir_entry_t *)dir_bulk_buf;
    uint32_t total = scnt * 16;
    char fat_name[11];
    to_fat_name(filename, fat_name);
    int found_idx = -1;
    int lfn_seq_start = -1;
    int in_lfn = 0;
    char lfn_buf[64];
    memset(lfn_buf, 0, 64);
    for (uint32_t i = 0; i < total; i++) {
        uint8_t fc = (uint8_t)entries[i].filename[0];
        if (fc == 0)
            break;
        if (fc == 0xE5) {
            in_lfn = 0;
            lfn_seq_start = -1;
            memset(lfn_buf, 0, 64);
            continue;
        }
        if ((entries[i].attributes & 0x0F) == 0x0F) {
            fat_lfn_entry_t *lfn = (fat_lfn_entry_t *)&entries[i];
            if (lfn->sequence & 0x40) {
                lfn_seq_start = (int)i;
                in_lfn = 1;
                memset(lfn_buf, 0, 64);
            }
            if (in_lfn) {
                int seq = lfn->sequence & 0x3F;
                if (seq >= 1 && seq <= 4)
                    lfn_extract_chars(lfn, seq - 1, lfn_buf, 64);
            }
            continue;
        }
        int is_match = 0;
        if (in_lfn && lfn_buf[0]) {
            const char *a = filename, *b = lfn_buf;
            int match = 1;
            while (*a && *b) {
                char ca = *a, cb = *b;
                if (ca >= 'a' && ca <= 'z')
                    ca -= 32;
                if (cb >= 'a' && cb <= 'z')
                    cb -= 32;
                if (ca != cb) {
                    match = 0;
                    break;
                }
                a++;
                b++;
            }
            if (!*a && !*b)
                is_match = match;
        }
        if (!is_match && memcmp(entries[i].filename, fat_name, 11) == 0)
            is_match = 1;
        if (is_match) {
            found_idx = (int)i;
            break;
        }
        in_lfn = 0;
        lfn_seq_start = -1;
        memset(lfn_buf, 0, 64);
    }
    if (found_idx < 0)
        return -1;
    fat_dir_entry_t existing;
    memcpy(&existing, &entries[found_idx], sizeof(existing));
    uint32_t fc = entry_cluster(&existing);
    if (fc >= 2)
        fat_free_chain(fc);
    int first_src = (lfn_seq_start >= 0 && in_lfn) ? lfn_seq_start : found_idx;
    for (int k = first_src; k <= found_idx; k++) {
        uint32_t slba = lba + ((uint32_t)k / 16);
        int sidx = k % 16;
        uint8_t buf[512];
        storage_read_sector(slba, buf);
        buf[sidx * 32] = 0xE5;
        storage_write_sector(slba, buf);
    }
    return 0;
}
static int switch_to_disk(int idx) {
    if (idx == current_disk_idx)
        return 0;
    int ndisks = qk_disk_count();
    if (idx < 0 || idx >= ndisks)
        return -1;

    fat_cache_flush();

    if (current_file_data) {
        free(current_file_data);
        current_file_data = 0;
    }
    current_file_size = 0;
    current_file_page_count = 0;
    current_file_page_offset = 0;
    reset_save_session();

    current_disk_idx = idx;
    qk_disk_select(idx);
    uint8_t boot_buf[512];
    if (read_current_boot_sector(boot_buf) != 0) {
        current_disk_idx = 0;
        qk_disk_select(0);
        current_partition_lba = 0;
        return -1;
    }
    fat_boot_sector_t *bs = (fat_boot_sector_t *)boot_buf;

    if (bs->bytes_per_sector == 0 || bs->sectors_per_cluster == 0) {
        fat.bytes_per_sector = 512;
        fat.sectors_per_cluster = 1;
        fat.sectors_per_fat = 0;
        fat.dir_entry_count = 0;
        fat.fat_count = 0;
        fat.reserved_sectors = 0;
        fat.bytes_per_cluster = 512;
        fat_start_lba = 0;
        root_dir_lba = 0;
        root_sectors = 0;
        data_start_lba = 0;
        is_fat32 = 0;
        root_cluster = 0;
        fat_cached_sector = 0xFFFFFFFFu;
        fat_cache_dirty = 0;
        fat_next_free_hint = 2;
        return 0;
    }
    fat.bytes_per_sector = bs->bytes_per_sector;
    fat.sectors_per_cluster = bs->sectors_per_cluster;
    fat.dir_entry_count = bs->dir_entry_count;
    fat.fat_count = bs->fat_count;
    fat.reserved_sectors = bs->reserved_sectors;
    fat.bytes_per_cluster = fat.bytes_per_sector * fat.sectors_per_cluster;
    is_fat32 = (bs->sectors_per_fat == 0);
    if (is_fat32) {
        fat.sectors_per_fat = FAT32_SECTORS_PER_FAT(boot_buf);
        root_cluster = FAT32_ROOT_CLUSTER(boot_buf);
    } else {
        fat.sectors_per_fat = bs->sectors_per_fat;
        root_cluster = 0;
    }
    fat_start_lba = fat.reserved_sectors;
    root_dir_lba = fat_start_lba + (fat.fat_count * fat.sectors_per_fat);
    if (is_fat32) {
        root_sectors = 0;
        data_start_lba = root_dir_lba;
    } else {
        root_sectors = (fat.dir_entry_count * 32 + 511) / 512;
        data_start_lba = root_dir_lba + root_sectors;
    }
    fat_cached_sector = 0xFFFFFFFFu;
    fat_cache_dirty = 0;
    fat_next_free_hint = 2;
    return 0;
}
#define MAX_DISK_SUBSCRIBERS 4
static int disk_subscribers[MAX_DISK_SUBSCRIBERS];
static int disk_sub_count = 0;

static void notify_disk_changed(int disk_idx, int mounted) {
    for (int i = 0; i < disk_sub_count; i++) {
        message_t m;
        memset(&m, 0, sizeof(m));
        m.type = MSG_DISK_CHANGED;
        m.data1 = (uint32_t)disk_idx;
        m.data2 = (uint32_t)mounted;
        qk_sys_ipc_send(disk_subscribers[i], &m);
    }
}
void main() {
    qk_set_proc_name(-1, "storage");
    qk_register_as_server(QK_SERVER_STORAGE);
    uint8_t boot_buf[512];
    int rd = read_current_boot_sector(boot_buf);
    if (rd != 0) {
        message_t msg;
        while (1) {
            qk_ipc_recv(&msg);
            message_t resp = {.type = MSG_ERROR, .data1 = (uint32_t)-1};
            qk_sys_ipc_send(msg.sender_pid, &resp);
        }
    }
    fat_boot_sector_t *bs = (fat_boot_sector_t *)boot_buf;
    fat.bytes_per_sector = bs->bytes_per_sector;
    fat.sectors_per_cluster = bs->sectors_per_cluster;
    fat.dir_entry_count = bs->dir_entry_count;
    fat.fat_count = bs->fat_count;
    fat.reserved_sectors = bs->reserved_sectors;
    fat.bytes_per_cluster = fat.bytes_per_sector * fat.sectors_per_cluster;
    is_fat32 = (bs->sectors_per_fat == 0);
    if (is_fat32) {
        fat.sectors_per_fat = FAT32_SECTORS_PER_FAT(boot_buf);
        root_cluster = FAT32_ROOT_CLUSTER(boot_buf);
    } else {
        fat.sectors_per_fat = bs->sectors_per_fat;
        root_cluster = 0;
    }
    fat_start_lba = fat.reserved_sectors;
    root_dir_lba = fat_start_lba + (fat.fat_count * fat.sectors_per_fat);
    if (is_fat32) {
        root_sectors = 0;
        data_start_lba = root_dir_lba;
    } else {
        root_sectors = (fat.dir_entry_count * 32 + 511) / 512;
        data_start_lba = root_dir_lba + root_sectors;
    }
    message_t msg;
    while (1) {
        qk_ipc_recv(&msg);
        if (msg.type == MSG_DISK_LIST) {
            message_t r;
            memset(&r, 0, sizeof(r));
            r.type = MSG_DISK_LIST_REPLY;
            int count = qk_disk_count();
            r.data1 = (uint32_t)count;

            int max_entries = count;
            if (max_entries > (int)(sizeof(r.payload) / sizeof(disk_info_t)))
                max_entries = (int)(sizeof(r.payload) / sizeof(disk_info_t));
            for (int i = 0; i < max_entries; i++)
                qk_disk_info(i, (disk_info_t *)(r.payload + i * sizeof(disk_info_t)));
            qk_sys_ipc_send(msg.sender_pid, &r);
        } else if (msg.type == MSG_DISK_EJECT) {
            int idx = (int)msg.data1;
            message_t r;
            memset(&r, 0, sizeof(r));
            if (idx <= 0 || idx >= MAX_DISKS) {
                r.type = MSG_ERROR;
            } else {
                disk_mounted[idx] = 0;
                if (current_disk_idx == idx)
                    switch_to_disk(0);
                r.type = MSG_OK;
                notify_disk_changed(idx, 0);
            }
            qk_sys_ipc_send(msg.sender_pid, &r);
        } else if (msg.type == MSG_DISK_MOUNT) {
            int idx = (int)msg.data1;
            message_t r;
            memset(&r, 0, sizeof(r));
            if (idx < 0 || idx >= MAX_DISKS) {
                r.type = MSG_ERROR;
            } else {
                disk_mounted[idx] = 1;
                r.type = MSG_OK;
                notify_disk_changed(idx, 1);
            }
            qk_sys_ipc_send(msg.sender_pid, &r);
        } else if (msg.type == MSG_DISK_SUBSCRIBE) {
            if (disk_sub_count < MAX_DISK_SUBSCRIBERS)
                disk_subscribers[disk_sub_count++] = msg.sender_pid;

            for (int i = 0; i < MAX_DISKS; i++) {
                if (!disk_mounted[i]) {
                    message_t n2;
                    memset(&n2, 0, sizeof(n2));
                    n2.type = MSG_DISK_CHANGED;
                    n2.data1 = (uint32_t)i;
                    n2.data2 = 0;
                    qk_sys_ipc_send(msg.sender_pid, &n2);
                }
            }
            message_t r;
            memset(&r, 0, sizeof(r));
            r.type = MSG_OK;
            qk_sys_ipc_send(msg.sender_pid, &r);
        } else if (msg.type == MSG_DISK_STATUS) {
            message_t r;
            memset(&r, 0, sizeof(r));
            r.type = MSG_OK;
            uint32_t bits = 0;
            for (int i = 0; i < MAX_DISKS; i++)
                if (disk_mounted[i])
                    bits |= (1u << i);
            r.data1 = bits;
            qk_sys_ipc_send(msg.sender_pid, &r);
        } else if (msg.type == MSG_FILE_OPEN) {
            int req_disk2 = (int)msg.data3;
            if (req_disk2 != current_disk_idx)
                switch_to_disk(req_disk2);
            if (req_disk2 >= 0 && req_disk2 < MAX_DISKS && !disk_mounted[req_disk2]) {
                message_t r;
                memset(&r, 0, sizeof(r));
                r.type = MSG_ERROR;
                qk_sys_ipc_send(msg.sender_pid, &r);
            } else {
                if (current_file_data) {
                    free(current_file_data);
                    current_file_data = 0;
                }
                current_file_page_count = 0;
                current_file_page_offset = 0;
                char fat_n[11];
                uint32_t dir_cluster;
                if (msg.data1 == 0xFFFFu) {
                    if (fat_resolve_path((char *)msg.payload, &dir_cluster, fat_n) != 0)
                        dir_cluster = 0xFFFFFFFEu;
                } else {
                    to_fat_name((char *)msg.payload, fat_n);
                    dir_cluster = msg.data1;
                }

                const char *leaf = (char *)msg.payload;
                if (msg.data1 == 0xFFFFu) {
                    const char *sl = leaf;
                    for (const char *s = leaf; *s; s++)
                        if (*s == '/')
                            sl = s + 1;
                    leaf = sl;
                }
                fat_dir_entry_t entry;
                bool found = false;
                if (dir_cluster != 0xFFFFFFFEu &&
                    fat_find_entry_lfn(dir_cluster, leaf, &entry) == 0) {
                    if (!(entry.attributes & 0x10)) {
                        current_file_size = entry.file_size;
                        current_file_data =
                            fat_read_file_server(entry_cluster(&entry), current_file_size);
                        found = true;
                    }
                }
                message_t r;
                memset(&r, 0, sizeof(r));
                r.type = found ? MSG_FILE_SIZE : MSG_ERROR;
                r.data1 = current_file_size;
                uint32_t pages_needed = (current_file_size + 4095) / 4096;
                if (found && current_file_data && current_file_size > 0 &&
                    pages_needed <= SHARED_MAX_PAGES) {
                    int cnt = qk_virt_to_phys_list((uint32_t)(uintptr_t)current_file_data,
                                                   current_file_size, current_file_pages);
                    if (cnt > 0 && cnt <= SHARED_MAX_PAGES) {
                        current_file_page_count = (uint32_t)cnt;
                        current_file_page_offset = (uint32_t)(uintptr_t)current_file_data & 0xFFF;
                        r.data2 = current_file_page_count;
                        r.data3 = current_file_page_offset;
                        uint32_t first_batch =
                            current_file_page_count < 128 ? current_file_page_count : 128;
                        memcpy(r.payload, current_file_pages, first_batch * sizeof(uint32_t));
                    }
                }
                qk_sys_ipc_send(msg.sender_pid, &r);
            }
        } else if (msg.type == MSG_FILE_GET_PAGES) {
            message_t r;
            memset(&r, 0, sizeof(r));
            uint32_t pg_off = msg.data1;
            if (pg_off < current_file_page_count) {
                uint32_t avail = current_file_page_count - pg_off;
                uint32_t send = avail < 128 ? avail : 128;
                memcpy(r.payload, current_file_pages + pg_off, send * sizeof(uint32_t));
                r.type = MSG_FILE_SIZE;
                r.data1 = send;
            } else {
                r.type = MSG_ERROR;
            }
            qk_sys_ipc_send(msg.sender_pid, &r);
        } else if (msg.type == MSG_REQ_DATA) {
            uint32_t off = msg.data1;
            uint32_t len = msg.data2;
            if (len > 4088)
                len = 4088;
            message_t r = {.type = MSG_FILE_DATA};
            if (current_file_data && off < current_file_size) {
                if (off + len > current_file_size)
                    len = current_file_size - off;
                memcpy(r.payload, current_file_data + off, len);
                r.data1 = len;
            } else
                r.data1 = 0;
            qk_sys_ipc_send(msg.sender_pid, &r);
        } else if (msg.type == MSG_DIR_REQ) {
            int req_disk = (int)msg.data3;
            if (req_disk != current_disk_idx)
                switch_to_disk(req_disk);
            if (req_disk >= 0 && req_disk < MAX_DISKS && !disk_mounted[req_disk]) {
                message_t r = {.type = MSG_DIR_REPLY, .data1 = 0};
                qk_sys_ipc_send(msg.sender_pid, &r);
            } else {
                uint32_t cluster = msg.data1;
                int fat16_root = (!is_fat32 && cluster == 0);
                if (is_fat32 && cluster == 0)
                    cluster = root_cluster;
                message_t r = {.type = MSG_DIR_REPLY};
                int lp = 0;
                char lb[128];
                bzero(lb, 128);
                char lfn_buf[64];
                int lfn_valid = 0;
                memset(lfn_buf, 0, 64);
                int done = 0;
                /* Follow the FAT chain across all clusters of the directory */
                uint32_t cur_cluster = cluster;
                while (!done) {
                    uint32_t lba = fat16_root
                                       ? root_dir_lba
                                       : (data_start_lba + (cur_cluster - 2) * fat.sectors_per_cluster);
                    uint32_t scnt = fat16_root ? root_sectors : fat.sectors_per_cluster;
                    if (scnt > DIR_BULK_MAX_SECTORS)
                        scnt = DIR_BULK_MAX_SECTORS;
                    virtio_read_sectors(lba, scnt, dir_bulk_buf);
                    fat_dir_entry_t *e = (fat_dir_entry_t *)dir_bulk_buf;
                    uint32_t total_dir_entries = scnt * 16;
                    for (uint32_t di = 0; di < total_dir_entries; di++) {
                        uint8_t first_char = (uint8_t)e[di].filename[0];
                        if (first_char == 0) {
                            done = 1;
                            break;
                        }
                        if (first_char == 0xE5) {
                            lfn_valid = 0;
                            memset(lfn_buf, 0, 64);
                            continue;
                        }
                        if (e[di].attributes & 0x08)
                            continue;

                        if ((e[di].attributes & 0x0F) == 0x0F) {
                            fat_lfn_entry_t *lfn = (fat_lfn_entry_t *)&e[di];
                            int seq = lfn->sequence & 0x3F;
                            if (lfn->sequence & 0x40) {
                                memset(lfn_buf, 0, 64);
                                lfn_valid = 1;
                            }
                            if (lfn_valid && seq >= 1 && seq <= 4)
                                lfn_extract_chars(lfn, seq - 1, lfn_buf, 64);
                            continue;
                        }

                        char name[64];
                        if (lfn_valid && lfn_buf[0]) {
                            strncpy(name, lfn_buf, 63);
                            name[63] = 0;
                        } else {
                            int j = 0;
                            for (int k = 0; k < 8 && e[di].filename[k] != ' '; k++) {
                                char c = e[di].filename[k];
                                if (k == 0) {
                                } else if (c >= 'A' && c <= 'Z')
                                    c += 32;
                                name[j++] = c;
                            }
                            if (e[di].ext[0] != ' ') {
                                name[j++] = '.';
                                for (int k = 0; k < 3 && e[di].ext[k] != ' '; k++) {
                                    char c = e[di].ext[k];
                                    if (c >= 'A' && c <= 'Z')
                                        c += 32;
                                    name[j++] = c;
                                }
                            }
                            name[j] = 0;
                        }
                        lfn_valid = 0;
                        memset(lfn_buf, 0, 64);
                        char info[80];
                        bzero(info, 80);
                        int is_d = (e[di].attributes & 0x10) ? 1 : 0;
                        int n = int_to_str(is_d, info, 0);
                        info[n++] = '|';
                        n += int_to_str(entry_cluster(&e[di]), info + n, 0);
                        info[n++] = '|';
                        strncpy(info + n, name, 63);
                        int ilen = strlen(info);
                        if (lp + ilen + 2 > 127) {
                            memcpy(r.payload, lb, 128);
                            r.data1 = 1;
                            qk_sys_ipc_send(msg.sender_pid, &r);
                            lp = 0;
                            bzero(lb, 128);
                        }
                        memcpy(lb + lp, info, ilen);
                        lp += ilen;
                        lb[lp++] = '\n';
                    }
                    /* Advance to next cluster in chain (skip for FAT16 fixed root) */
                    if (!done && !fat16_root) {
                        uint32_t next = fat_get_entry(cur_cluster);
                        uint32_t eof = is_fat32 ? 0x0FFFFFF8u : 0xFFF8u;
                        if (next >= eof)
                            done = 1;
                        else
                            cur_cluster = next;
                    } else {
                        done = 1;
                    }
                }
                memcpy(r.payload, lb, 128);
                r.data1 = 0;
                qk_sys_ipc_send(msg.sender_pid, &r);
            }
        } else if (msg.type == MSG_FILE_MOVE) {
            int disk = (int)msg.data1;
            uint32_t src_dir_cluster = msg.data2;
            const char *src_filename = (const char *)msg.payload;
            const char *dst_path = src_filename + strlen(src_filename) + 1;
            message_t r;
            memset(&r, 0, sizeof(r));
            if (switch_to_disk(disk) == 0 && src_filename[0] && dst_path[0]) {
                uint32_t dst_cluster = fat_resolve_dir_cluster(dst_path);
                if (dst_cluster == FAT_BAD_CLUSTER) {
                    r.type = MSG_ERROR;
                } else {
                    int res = fat_move_file_in_dir(src_dir_cluster, dst_cluster, src_filename);
                    r.type = (res == 0) ? MSG_OK : MSG_ERROR;
                }
            } else {
                r.type = MSG_ERROR;
            }
            qk_sys_ipc_send(msg.sender_pid, &r);
        } else if (msg.type == MSG_FILE_DELETE) {
            int disk = (int)msg.data1;
            uint32_t dir_cluster = msg.data2;
            const char *filename = (const char *)msg.payload;
            message_t r;
            memset(&r, 0, sizeof(r));
            if (switch_to_disk(disk) == 0 && filename[0]) {
                int res = fat_delete_file_in_dir(dir_cluster, filename);
                r.type = (res == 0) ? MSG_OK : MSG_ERROR;
            } else {
                r.type = MSG_ERROR;
            }
            qk_sys_ipc_send(msg.sender_pid, &r);
        } else if (msg.type == MSG_DIR_CREATE) {
            const char *name = (const char *)msg.payload;
            int res = fat_create_directory_entry(msg.data1, name);
            message_t r;
            memset(&r, 0, sizeof(r));
            r.type = (res == 0) ? MSG_OK : MSG_ERROR;
            qk_sys_ipc_send(msg.sender_pid, &r);
        } else if (msg.type == MSG_FILE_SAVE_BEGIN) {
            int save_disk = (int)msg.data3;
            if (save_disk != current_disk_idx)
                switch_to_disk(save_disk);
            message_t r;
            memset(&r, 0, sizeof(r));
            if (save_disk >= 0 && save_disk < MAX_DISKS && !disk_mounted[save_disk]) {
                r.type = MSG_ERROR;
            } else if (save_session.active) {
                r.type = MSG_ERROR;
            } else {
                uint32_t size = msg.data2;
                uint8_t *buffer = (size > 0) ? malloc(size) : 0;
                if (size > 0 && !buffer) {
                    r.type = MSG_ERROR;
                } else {
                    memset(&save_session, 0, sizeof(save_session));
                    save_session.active = 1;
                    save_session.sender_pid = msg.sender_pid;
                    save_session.dir_cluster = msg.data1;
                    save_session.expected_size = size;
                    save_session.received_size = 0;
                    save_session.buffer = buffer;
                    strncpy(save_session.filename, (char *)msg.payload,
                            sizeof(save_session.filename) - 1);
                    save_session.filename[sizeof(save_session.filename) - 1] = 0;
                    r.type = MSG_OK;
                }
            }
            qk_sys_ipc_send(msg.sender_pid, &r);
        } else if (msg.type == MSG_FILE_SAVE_CHUNK) {
            message_t r;
            memset(&r, 0, sizeof(r));
            uint32_t chunk = msg.data1;
            if (!save_session.active || msg.sender_pid != save_session.sender_pid) {
                r.type = MSG_ERROR;
            } else if (save_session.received_size + chunk > save_session.expected_size) {
                r.type = MSG_ERROR;
                reset_save_session();
            } else if (chunk > 0 && save_session.expected_size > 0 && !save_session.buffer) {
                r.type = MSG_ERROR;
                reset_save_session();
            } else {
                if (chunk > 0 && save_session.buffer) {
                    memcpy(save_session.buffer + save_session.received_size, msg.payload, chunk);
                }
                save_session.received_size += chunk;
                r.type = MSG_OK;
            }
            qk_sys_ipc_send(msg.sender_pid, &r);
        } else if (msg.type == MSG_FILE_SAVE_END) {
            message_t r;
            memset(&r, 0, sizeof(r));
            if (!save_session.active || msg.sender_pid != save_session.sender_pid ||
                save_session.received_size != save_session.expected_size) {
                r.type = MSG_ERROR;
                qk_sys_ipc_send(msg.sender_pid, &r);
                reset_save_session();
            } else {
                int res = fat_save_file_to_dir(save_session.dir_cluster, save_session.filename,
                                               save_session.buffer, save_session.expected_size);
                r.type = (res == 0) ? MSG_OK : MSG_ERROR;
                qk_sys_ipc_send(msg.sender_pid, &r);
                reset_save_session();
            }
        }
    }
}
