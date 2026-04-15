#include "fs/fat.h"
#include "kernel/ipc.h"
#include "lib/stdbool.h"
#include "lib/stdlib.h"
#include "user/lib/libapi.h"
#define ATA_DATA 0x1F0
#define ATA_ERROR 0x1F1
#define ATA_SECTOR_CNT 0x1F2
#define ATA_LBA_LO 0x1F3
#define ATA_LBA_MID 0x1F4
#define ATA_LBA_HI 0x1F5
#define ATA_DRIVE_HEAD 0x1F6
#define ATA_STATUS 0x1F7
#define ATA_COMMAND 0x1F7
#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_STATUS_BSY 0x80
#define ATA_STATUS_DRQ 0x08
typedef struct {
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t dir_entry_count;
    uint16_t sectors_per_fat;
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
static uint16_t fat_next_free_hint = 2;
typedef struct {
    int active;
    int sender_pid;
    uint16_t dir_cluster;
    uint32_t expected_size;
    uint32_t received_size;
    char filename[64];
    uint8_t *buffer;
} save_session_t;
static save_session_t save_session;
static void reset_save_session(void)
{
    if (save_session.buffer) {
        free(save_session.buffer);
    }
    memset(&save_session, 0, sizeof(save_session));
}
#define PCI_ADDR_PORT 0xCF8
#define PCI_DATA_PORT 0xCFC
#define BM_CMD    0x00
#define BM_STATUS 0x02
#define BM_PRDT   0x04
#define BM_CMD_START 0x01
#define BM_CMD_READ  0x08
#define BM_ST_ERROR 0x02
#define BM_ST_IRQ   0x04
typedef struct {
    uint32_t phys_addr;
    uint16_t byte_count; 
    uint16_t flags;      
} __attribute__((packed)) prd_t;
static uint16_t bm_base       = 0;
static uint8_t *prdt_buf      = 0; 
static uint32_t prdt_phys     = 0;
static uint32_t pci_read32(uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t addr = (1u << 31) | ((uint32_t)dev << 11) | ((uint32_t)func << 8) | (off & 0xFC);
    qk_hw_outl(PCI_ADDR_PORT, addr);
    return qk_hw_inl(PCI_DATA_PORT);
}
static void pci_write32(uint8_t dev, uint8_t func, uint8_t off, uint32_t val)
{
    uint32_t addr = (1u << 31) | ((uint32_t)dev << 11) | ((uint32_t)func << 8) | (off & 0xFC);
    qk_hw_outl(PCI_ADDR_PORT, addr);
    qk_hw_outl(PCI_DATA_PORT, val);
}
static void bm_dma_init(void)
{
    for (uint8_t d = 0; d < 32; d++) {
        uint32_t id = pci_read32(d, 0, 0x00);
        if ((id & 0xFFFF) == 0xFFFF)
            continue;
        uint32_t cc = pci_read32(d, 0, 0x08);
        uint8_t cls = (cc >> 24) & 0xFF;
        uint8_t sub = (cc >> 16) & 0xFF;
        if (cls != 0x01 || sub != 0x01)
            continue;
        uint32_t cmd = pci_read32(d, 0, 0x04);
        pci_write32(d, 0, 0x04, cmd | 0x04);
        uint32_t bar4 = pci_read32(d, 0, 0x20);
        bm_base = (uint16_t)(bar4 & ~0x3u);
        prdt_buf = (uint8_t *)malloc(8192);
        if (!prdt_buf) { bm_base = 0; return; }
        for (uint32_t i = 0; i < 8192; i += 4096)
            ((volatile uint8_t *)prdt_buf)[i] = 0;
        uint32_t aligned_virt = ((uint32_t)prdt_buf + 4095) & ~0xFFFu;
        uint32_t phys_out[2];
        if (qk_virt_to_phys_list(aligned_virt, 4096, phys_out) < 1) {
            free(prdt_buf); prdt_buf = 0; bm_base = 0; return;
        }
        prdt_phys = phys_out[0];
        prdt_buf  = (uint8_t *)aligned_virt; 
        return;
    }
}
static int ata_dma_read_sectors(uint32_t lba, uint32_t count, uint8_t *buffer)
{
    if (bm_base == 0 || count == 0 || count > 255)
        return qk_ata_read_sectors(lba, count, buffer);
    uint32_t total_bytes = count * 512;
    uint32_t buf_off     = (uint32_t)buffer & 0xFFFu;
    uint32_t n_pages_max = (buf_off + total_bytes + 4095) / 4096;
    if (n_pages_max > 64)
        return qk_ata_read_sectors(lba, count, buffer);
    uint32_t phys_pages[64];
    int np = qk_virt_to_phys_list((uint32_t)buffer, total_bytes, phys_pages);
    if (np <= 0)
        return qk_ata_read_sectors(lba, count, buffer);
    prd_t *prdt = (prd_t *)prdt_buf;
    uint32_t remaining = total_bytes;
    for (int i = 0; i < np && remaining > 0; i++) {
        uint32_t start = phys_pages[i] + (i == 0 ? buf_off : 0u);
        uint32_t chunk = (i == 0) ? (4096u - buf_off) : 4096u;
        if (chunk > remaining) chunk = remaining;
        prdt[i].phys_addr  = start;
        prdt[i].byte_count = (uint16_t)(chunk & 0xFFFFu); 
        prdt[i].flags      = 0;
        remaining -= chunk;
    }
    prdt[np - 1].flags = 0x8000; 
    qk_hw_outl(bm_base + BM_PRDT, prdt_phys);
    qk_hw_outb(bm_base + BM_STATUS, BM_ST_ERROR | BM_ST_IRQ); 
    qk_hw_outb(bm_base + BM_CMD, BM_CMD_READ);                
    for (int t = 0; t < 100000; t++) { if (!(qk_hw_inb(ATA_STATUS) & ATA_STATUS_BSY)) break; }
    qk_hw_outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    qk_hw_outb(ATA_ERROR, 0x00);
    qk_hw_outb(ATA_SECTOR_CNT, (uint8_t)count);
    qk_hw_outb(ATA_LBA_LO, (uint8_t)(lba));
    qk_hw_outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    qk_hw_outb(ATA_LBA_HI, (uint8_t)(lba >> 16));
    qk_hw_outb(ATA_COMMAND, 0xC8); 
    qk_hw_outb(bm_base + BM_CMD, BM_CMD_READ | BM_CMD_START);
    int ok = 1;
    for (uint32_t i = 0; i < 2000000u; i++) {
        uint8_t st = qk_hw_inb(bm_base + BM_STATUS);
        if (st & BM_ST_ERROR) { ok = 0; break; }
        if (st & BM_ST_IRQ)   break;
    }
    qk_hw_outb(bm_base + BM_CMD, 0x00);
    qk_hw_outb(bm_base + BM_STATUS, BM_ST_ERROR | BM_ST_IRQ);
    qk_hw_inb(ATA_STATUS); 
    if (!ok)
        return qk_ata_read_sectors(lba, count, buffer); 
    return 0;
}
static int ata_error = 0;
void ata_wait_bsy()
{
    for (uint32_t i = 0; i < 100000; i++) {
        if (!(qk_hw_inb(ATA_STATUS) & ATA_STATUS_BSY))
            return;
    }
    ata_error = 1;
}
void ata_wait_drq()
{
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t st = qk_hw_inb(ATA_STATUS);
        if (st & ATA_STATUS_DRQ)
            return;
        if (st & 0x01) {
            ata_error = 1;
            return;
        }
    }
    ata_error = 1;
}
int drv_stor_ata_read(uint32_t lba, uint8_t *buffer)
{
    return qk_ata_read_sectors(lba, 1, buffer);
}
int drv_stor_ata_write(uint32_t lba, uint8_t *buffer)
{
    ata_error = 0;
    ata_wait_bsy();
    if (ata_error)
        return -1;
    qk_hw_outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    qk_hw_outb(ATA_ERROR, 0x00);
    qk_hw_outb(ATA_SECTOR_CNT, 1);
    qk_hw_outb(ATA_LBA_LO, (uint8_t)(lba & 0xFF));
    qk_hw_outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    qk_hw_outb(ATA_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    qk_hw_outb(ATA_COMMAND, ATA_CMD_WRITE_PIO);
    ata_wait_bsy();
    ata_wait_drq();
    if (ata_error)
        return -1;
    uint16_t *src = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++)
        qk_outw(ATA_DATA, src[i]);
    return 0;
}
static int drv_stor_ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buffer)
{
    if (count == 0)
        return 0;
    ata_error = 0;
    ata_wait_bsy();
    if (ata_error)
        return -1;
    qk_hw_outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    qk_hw_outb(ATA_ERROR, 0x00);
    qk_hw_outb(ATA_SECTOR_CNT, count);
    qk_hw_outb(ATA_LBA_LO, (uint8_t)(lba & 0xFF));
    qk_hw_outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    qk_hw_outb(ATA_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    qk_hw_outb(ATA_COMMAND, ATA_CMD_WRITE_PIO);
    for (int s = 0; s < count; s++) {
        ata_wait_bsy();
        ata_wait_drq();
        if (ata_error)
            return -1;
        const uint16_t *src = (const uint16_t *)(buffer + s * 512);
        for (int i = 0; i < 256; i++)
            qk_outw(ATA_DATA, src[i]);
    }
    return 0;
}
void to_fat_name(const char *src, char *dest)
{
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
static void fat_cache_flush(void)
{
    if (fat_cache_dirty && fat_cached_sector != 0xFFFFFFFFu) {
        drv_stor_ata_write(fat_cached_sector, fat_cache_buf);
        fat_cache_dirty = 0;
    }
}
static void fat_cache_load(uint32_t sector)
{
    if (fat_cached_sector != sector) {
        fat_cache_flush();
        drv_stor_ata_read(sector, fat_cache_buf);
        fat_cached_sector = sector;
    }
}
uint16_t fat_get_entry(uint16_t cluster)
{
    uint32_t sector = fat_start_lba + ((cluster * 2) / fat.bytes_per_sector);
    fat_cache_load(sector);
    uint32_t offset = (cluster * 2) % fat.bytes_per_sector;
    return *(uint16_t *)&fat_cache_buf[offset];
}
uint8_t *fat_read_file_server(uint16_t cluster, uint32_t size)
{
    if (size == 0)
        return 0;
    uint8_t *data = malloc(size + fat.bytes_per_cluster + 512);
    if (!data)
        return 0;
    uint32_t alloc_size = size + fat.bytes_per_cluster + 512;
    for (uint32_t i = 0; i < alloc_size; i += 4096)
        ((volatile uint8_t *)data)[i] = 0;
    uint8_t *ptr = data;
    uint16_t cur = cluster;
    uint32_t total_read = 0;
    while (cur < 0xFFF8 && cur != 0xFFFF && total_read < size) {
        uint16_t run_start = cur;
        uint32_t run_clusters = 1;
        uint16_t next = fat_get_entry(cur);
        while (next == cur + 1 && next < 0xFFF8 && next != 0xFFFF) {
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
            ata_dma_read_sectors(lba, batch, rptr);
            lba    += batch;
            rptr   += batch * 512;
            sectors -= batch;
        }
        uint32_t run_bytes = run_clusters * fat.bytes_per_cluster;
        uint32_t left = size - total_read;
        uint32_t chunk = (left < run_bytes) ? left : run_bytes;
        ptr        += chunk;
        total_read += chunk;
    }
    return data;
}
#define DIR_BULK_MAX_SECTORS 32
static uint8_t dir_bulk_buf[DIR_BULK_MAX_SECTORS * 512];
static int fat_find_entry_in_dir(uint16_t dir_cluster, const char *fat_name,
                                 fat_dir_entry_t *out_entry)
{
    uint32_t lba = (dir_cluster == 0)
                       ? root_dir_lba
                       : (data_start_lba + (dir_cluster - 2) * fat.sectors_per_cluster);
    uint32_t scnt = (dir_cluster == 0) ? root_sectors : fat.sectors_per_cluster;
    if (scnt > DIR_BULK_MAX_SECTORS) scnt = DIR_BULK_MAX_SECTORS;
    ata_dma_read_sectors(lba, scnt, dir_bulk_buf);
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
static int fat_resolve_path(const char *path, uint16_t *out_dir_cluster, char *out_fat_name)
{
    uint16_t cluster = 0; 
    const char *p = path;
    if (*p == '/') p++;
    char seg[16];
    while (*p) {
        int si = 0;
        while (*p && *p != '/') {
            if (si < 15) seg[si++] = *p;
            p++;
        }
        seg[si] = 0;
        if (*p == '/') {
            char fat_n[11];
            to_fat_name(seg, fat_n);
            fat_dir_entry_t entry;
            if (fat_find_entry_in_dir(cluster, fat_n, &entry) != 0)
                return -1;
            if (!(entry.attributes & 0x10))
                return -1; 
            cluster = entry.first_cluster_low;
            p++; 
        } else {
            if (out_dir_cluster) *out_dir_cluster = cluster;
            if (out_fat_name)    to_fat_name(seg, out_fat_name);
            return 0;
        }
    }
    return -1;
}
static uint32_t cluster_to_lba(uint16_t cluster)
{
    if (cluster < 2)
        return data_start_lba;
    return data_start_lba + (cluster - 2) * fat.sectors_per_cluster;
}
static void fat_set_entry(uint16_t cluster, uint16_t value)
{
    uint32_t sector = fat_start_lba + ((cluster * 2) / fat.bytes_per_sector);
    fat_cache_load(sector);
    uint32_t offset = (cluster * 2) % fat.bytes_per_sector;
    *(uint16_t *)&fat_cache_buf[offset] = value;
    fat_cache_dirty = 1;
}
static uint16_t fat_find_free_cluster(void)
{
    uint32_t total_entries = (fat.sectors_per_fat * fat.bytes_per_sector) / 2u;
    if (total_entries < 2)
        return 0xFFFF;
    uint32_t start = fat_next_free_hint;
    if (start < 2)
        start = 2;
    for (uint32_t scanned = 0; scanned < total_entries; scanned++) {
        uint32_t cluster = start + scanned;
        if (cluster >= total_entries)
            cluster = 2 + (cluster - total_entries);
        if (cluster >= total_entries)
            cluster = 2;
        if (fat_get_entry((uint16_t)cluster) == 0) {
            fat_next_free_hint = (uint16_t)(cluster + 1);
            if (fat_next_free_hint >= total_entries)
                fat_next_free_hint = 2;
            return (uint16_t)cluster;
        }
    }
    return 0xFFFF;
}
static void fat_free_chain(uint16_t start_cluster)
{
    uint16_t cur = start_cluster;
    while (cur >= 2 && cur < 0xFFF8) {
        uint16_t next = fat_get_entry(cur);
        fat_set_entry(cur, 0);
        if (cur < fat_next_free_hint)
            fat_next_free_hint = cur;
        cur = next;
    }
    fat_cache_flush();
}
static int fat_allocate_chain(uint32_t size, uint16_t *out_first_cluster)
{
    if (!out_first_cluster)
        return -1;
    uint32_t clusters_needed = (size + fat.bytes_per_cluster - 1) / fat.bytes_per_cluster;
    if (clusters_needed == 0)
        clusters_needed = 1;
    uint16_t first = 0, prev = 0;
    for (uint32_t i = 0; i < clusters_needed; i++) {
        uint16_t cluster = fat_find_free_cluster();
        if (cluster == 0xFFFF) {
            if (first >= 2)
                fat_free_chain(first);
            return -1;
        }
        fat_set_entry(cluster, 0xFFFF);
        if (prev >= 2)
            fat_set_entry(prev, cluster);
        else
            first = cluster;
        prev = cluster;
    }
    fat_set_entry(prev, 0xFFFF);
    fat_cache_flush();
    *out_first_cluster = first;
    return 0;
}
static void fat_zero_cluster(uint16_t cluster)
{
    uint32_t lba = cluster_to_lba(cluster);
    uint32_t spc = fat.sectors_per_cluster;
    uint8_t *buf = malloc(spc * 512);
    if (buf) {
        memset(buf, 0, spc * 512);
        drv_stor_ata_write_sectors(lba, (uint8_t)spc, buf);
        free(buf);
    } else {
        uint8_t zero[512];
        memset(zero, 0, sizeof(zero));
        for (uint32_t i = 0; i < spc; i++)
            drv_stor_ata_write(lba + i, zero);
    }
}
static void fat_write_chain(uint16_t start_cluster, const uint8_t *data, uint32_t size)
{
    if (start_cluster < 2)
        return;
    uint32_t bytes_per_cluster = fat.bytes_per_cluster;
    uint32_t spc = fat.sectors_per_cluster;
    uint8_t *cbuf = malloc(bytes_per_cluster);
    if (!cbuf)
        return;
    uint16_t cur = start_cluster;
    uint32_t remaining = size;
    while (cur >= 2 && cur < 0xFFF8) {
        uint32_t lba = cluster_to_lba(cur);
        uint32_t to_copy = (remaining > bytes_per_cluster) ? bytes_per_cluster : remaining;
        if (to_copy > 0 && data)
            memcpy(cbuf, data, to_copy);
        if (to_copy < bytes_per_cluster)
            memset(cbuf + to_copy, 0, bytes_per_cluster - to_copy);
        drv_stor_ata_write_sectors(lba, (uint8_t)spc, cbuf);
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
static uint32_t dir_first_lba(uint16_t dir_cluster)
{
    return (dir_cluster == 0) ? root_dir_lba : cluster_to_lba(dir_cluster);
}
static uint32_t dir_sector_count(uint16_t dir_cluster)
{
    return (dir_cluster == 0) ? root_sectors : fat.sectors_per_cluster;
}
static int fat_lookup_entry_with_location(uint16_t dir_cluster, const char *fat_name,
                                          fat_dir_entry_t *out_entry, uint32_t *out_lba,
                                          int *out_index)
{
    uint32_t lba = dir_first_lba(dir_cluster);
    uint32_t scnt = dir_sector_count(dir_cluster);
    if (scnt > DIR_BULK_MAX_SECTORS) scnt = DIR_BULK_MAX_SECTORS;
    ata_dma_read_sectors(lba, scnt, dir_bulk_buf);
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
static int fat_find_free_entry_slot(uint16_t dir_cluster, uint32_t *out_lba, int *out_index)
{
    uint32_t lba = dir_first_lba(dir_cluster);
    uint32_t scnt = dir_sector_count(dir_cluster);
    if (scnt > DIR_BULK_MAX_SECTORS) scnt = DIR_BULK_MAX_SECTORS;
    ata_dma_read_sectors(lba, scnt, dir_bulk_buf);
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
static int fat_write_entry_at(uint32_t lba, int entry_index, const fat_dir_entry_t *entry)
{
    uint8_t buf[512];
    drv_stor_ata_read(lba, buf);
    fat_dir_entry_t *entries = (fat_dir_entry_t *)buf;
    entries[entry_index] = *entry;
    drv_stor_ata_write(lba, buf);
    return 0;
}
static void fat_fill_entry_name(fat_dir_entry_t *entry, const char *fat_name)
{
    memcpy(entry->filename, fat_name, 8);
    memcpy(entry->ext, fat_name + 8, 3);
}
static int fat_save_file_to_dir(uint16_t dir_cluster, const char *filename, const uint8_t *data,
                                uint32_t size)
{
    char fat_name[11];
    to_fat_name(filename, fat_name);
    fat_dir_entry_t existing;
    uint32_t entry_lba = 0;
    int entry_index = -1;
    bool found = (fat_lookup_entry_with_location(dir_cluster, fat_name, &existing, &entry_lba,
                                                 &entry_index) == 0);
    if (found && (existing.attributes & 0x10))
        return -1;
    uint16_t new_first_cluster = 0;
    if (size > 0) {
        if (fat_allocate_chain(size, &new_first_cluster) != 0)
            return -1;
        fat_write_chain(new_first_cluster, data, size);
    }
    if (found && existing.first_cluster_low >= 2) {
        fat_free_chain(existing.first_cluster_low);
    }
    fat_dir_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    fat_fill_entry_name(&entry, fat_name);
    entry.attributes = 0x20;
    entry.first_cluster_low = new_first_cluster;
    entry.file_size = size;
    if (!found) {
        if (fat_find_free_entry_slot(dir_cluster, &entry_lba, &entry_index) != 0)
            return -1;
    }
    fat_write_entry_at(entry_lba, entry_index, &entry);
    return 0;
}
static void prepare_special_entry(fat_dir_entry_t *entry, const char *name, uint16_t cluster)
{
    memset(entry, 0, sizeof(*entry));
    memset(entry->filename, ' ', 8);
    memset(entry->ext, ' ', 3);
    for (int i = 0; name[i] && i < 8; i++)
        entry->filename[i] = name[i];
    entry->attributes = 0x10;
    entry->first_cluster_low = cluster;
}
static int fat_create_directory_entry(uint16_t parent_cluster, const char *name)
{
    char fat_name[11];
    to_fat_name(name, fat_name);
    if (fat_lookup_entry_with_location(parent_cluster, fat_name, 0, 0, 0) == 0)
        return -1;
    uint16_t new_cluster = fat_find_free_cluster();
    if (new_cluster == 0xFFFF)
        return -1;
    fat_set_entry(new_cluster, 0xFFFF);
    fat_zero_cluster(new_cluster);
    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));
    fat_dir_entry_t *entries = (fat_dir_entry_t *)buf;
    fat_dir_entry_t dot, dotdot;
    prepare_special_entry(&dot, ".", new_cluster);
    prepare_special_entry(&dotdot, "..", (parent_cluster == 0) ? 0 : parent_cluster);
    entries[0] = dot;
    entries[1] = dotdot;
    drv_stor_ata_write(cluster_to_lba(new_cluster), buf);
    fat_dir_entry_t new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    fat_fill_entry_name(&new_entry, fat_name);
    new_entry.attributes = 0x10;
    new_entry.first_cluster_low = new_cluster;
    uint32_t entry_lba;
    int entry_index;
    if (fat_find_free_entry_slot(parent_cluster, &entry_lba, &entry_index) != 0) {
        fat_free_chain(new_cluster);
        return -1;
    }
    fat_write_entry_at(entry_lba, entry_index, &new_entry);
    return 0;
}
void main()
{
    uint8_t boot_buf[512];
    drv_stor_ata_read(0, boot_buf);
    fat_boot_sector_t *bs = (fat_boot_sector_t *)boot_buf;
    fat.bytes_per_sector = bs->bytes_per_sector;
    fat.sectors_per_cluster = bs->sectors_per_cluster;
    fat.sectors_per_fat = bs->sectors_per_fat;
    fat.dir_entry_count = bs->dir_entry_count;
    fat.fat_count = bs->fat_count;
    fat.reserved_sectors = bs->reserved_sectors;
    fat.bytes_per_cluster = fat.bytes_per_sector * fat.sectors_per_cluster;
    fat_start_lba = fat.reserved_sectors;
    root_dir_lba = fat_start_lba + (fat.fat_count * fat.sectors_per_fat);
    root_sectors = (fat.dir_entry_count * 32 + 511) / 512;
    data_start_lba = root_dir_lba + root_sectors;
    bm_dma_init();
    message_t msg;
    while (1) {
        qk_ipc_recv(&msg);
        if (msg.type == MSG_FILE_OPEN) {
            if (current_file_data) {
                free(current_file_data);
                current_file_data = 0;
            }
            current_file_page_count = 0;
            current_file_page_offset = 0;
            char fat_n[11];
            uint16_t dir_cluster;
            if (msg.data1 == 0xFFFF) {
                if (fat_resolve_path((char *)msg.payload, &dir_cluster, fat_n) != 0)
                    dir_cluster = 0xFFFE; 
            } else {
                to_fat_name((char *)msg.payload, fat_n);
                dir_cluster = (uint16_t)msg.data1;
            }
            fat_dir_entry_t entry;
            bool found = false;
            if (dir_cluster != 0xFFFE && fat_find_entry_in_dir(dir_cluster, fat_n, &entry) == 0) {
                if (!(entry.attributes & 0x10)) {
                    current_file_size = entry.file_size;
                    current_file_data =
                        fat_read_file_server(entry.first_cluster_low, current_file_size);
                    found = true;
                }
            }
            message_t r;
            memset(&r, 0, sizeof(r));
            r.type  = found ? MSG_FILE_SIZE : MSG_ERROR;
            r.data1 = current_file_size;
            uint32_t pages_needed = (current_file_size + 4095) / 4096;
            if (found && current_file_data && current_file_size > 0
                    && pages_needed <= SHARED_MAX_PAGES) {
                int cnt = qk_virt_to_phys_list((uint32_t)current_file_data,
                                               current_file_size,
                                               current_file_pages);
                if (cnt > 0 && cnt <= SHARED_MAX_PAGES) {
                    current_file_page_count  = (uint32_t)cnt;
                    current_file_page_offset = (uint32_t)current_file_data & 0xFFF;
                    r.data2 = current_file_page_count;
                    r.data3 = current_file_page_offset;
                    uint32_t first_batch = current_file_page_count < 128
                                          ? current_file_page_count : 128;
                    memcpy(r.payload, current_file_pages,
                           first_batch * sizeof(uint32_t));
                }
            }
            qk_sys_ipc_send(msg.sender_pid, &r);
        } else if (msg.type == MSG_FILE_GET_PAGES) {
            message_t r;
            memset(&r, 0, sizeof(r));
            uint32_t pg_off = msg.data1;
            if (pg_off < current_file_page_count) {
                uint32_t avail = current_file_page_count - pg_off;
                uint32_t send  = avail < 128 ? avail : 128;
                memcpy(r.payload, current_file_pages + pg_off,
                       send * sizeof(uint32_t));
                r.type  = MSG_FILE_SIZE;   
                r.data1 = send;
            } else {
                r.type  = MSG_ERROR;
            }
            qk_sys_ipc_send(msg.sender_pid, &r);
        } else if (msg.type == MSG_REQ_DATA) {
            uint32_t off = msg.data1;
            uint32_t len = msg.data2;
            if (len > 504)
                len = 504;
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
            uint16_t cluster = (uint16_t)msg.data1;
            uint32_t lba = (cluster == 0)
                               ? root_dir_lba
                               : (data_start_lba + (cluster - 2) * fat.sectors_per_cluster);
            uint32_t scnt = (cluster == 0) ? root_sectors : fat.sectors_per_cluster;
            if (scnt > DIR_BULK_MAX_SECTORS) scnt = DIR_BULK_MAX_SECTORS;
            ata_dma_read_sectors(lba, scnt, dir_bulk_buf);
            message_t r = {.type = MSG_DIR_REPLY};
            int lp = 0;
            char lb[128];
            bzero(lb, 128);
            fat_dir_entry_t *e = (fat_dir_entry_t *)dir_bulk_buf;
            uint32_t total_dir_entries = scnt * 16;
            for (uint32_t di = 0; di < total_dir_entries; di++) {
                    uint8_t first_char = (uint8_t)e[di].filename[0];
                    if (first_char == 0)
                        goto end_d;
                    if (first_char == 0xE5 || (e[di].attributes & 0x08))
                        continue;
                    char name[16];
                    int j = 0;
                    for (int k = 0; k < 8 && e[di].filename[k] != ' '; k++)
                        name[j++] = e[di].filename[k];
                    if (e[di].ext[0] != ' ') {
                        name[j++] = '.';
                        for (int k = 0; k < 3 && e[di].ext[k] != ' '; k++)
                            name[j++] = e[di].ext[k];
                    }
                    name[j] = 0;
                    char info[64];
                    bzero(info, 64);
                    int is_d = (e[di].attributes & 0x10) ? 1 : 0;
                    int n = int_to_str(is_d, info, 0);
                    info[n++] = '|';
                    n += int_to_str(e[di].first_cluster_low, info + n, 0);
                    info[n++] = '|';
                    strncpy(info + n, name, 15);
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
        end_d:
            memcpy(r.payload, lb, 128);
            r.data1 = 0;
            qk_sys_ipc_send(msg.sender_pid, &r);
        } else if (msg.type == MSG_DIR_CREATE) {
            const char *name = (const char *)msg.payload;
            int res = fat_create_directory_entry((uint16_t)msg.data1, name);
            message_t r;
            memset(&r, 0, sizeof(r));
            r.type = (res == 0) ? MSG_OK : MSG_ERROR;
            qk_sys_ipc_send(msg.sender_pid, &r);
        } else if (msg.type == MSG_FILE_SAVE_BEGIN) {
            message_t r;
            memset(&r, 0, sizeof(r));
            if (save_session.active) {
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
                    save_session.dir_cluster = (uint16_t)msg.data1;
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
