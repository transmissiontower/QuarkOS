// SPDX-License-Identifier: MIT
/*
 * src/user/imgview.c - Image viewer with BMP and STB-based format support
 */
#include "kernel/ipc.h"
#include "lib/stdlib.h"
#include "user/lib/libapi.h"
#define WIN_W 400
#define WIN_H 300
#define _STDDEF_H
#define _STDDEF_H_
#define _ANSI_STDDEF_H
#define _GCC_WRAP_STDINT_H
#define _STDINT_H
#define _SYS_STDINT_H_
typedef unsigned int   size_t;
typedef int            ptrdiff_t;
#ifndef NULL
#define NULL ((void *)0)
#endif
#ifndef abs
#define abs(x) ((x) < 0 ? -(x) : (x))
#endif
#define STBI_MALLOC(sz)               malloc((unsigned int)(sz))
#define STBI_FREE(p)                  free(p)
#define STBI_REALLOC_SIZED(p,o,n)     realloc((p),(unsigned int)(n))
#define STBI_REALLOC(p,sz)            realloc((p),(unsigned int)(sz))
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_TGA
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_ASSERT(x) ((void)0)
#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_IMPLEMENTATION
#include "user/lib/stb_image.h"
typedef struct __attribute__((packed)) {
    uint8_t  sig[2];
    uint32_t file_size;
    uint16_t res1, res2;
    uint32_t pix_offset;
} bmp_fhdr_t;
typedef struct __attribute__((packed)) {
    uint32_t hdr_size;
    int32_t  width, height;
    uint16_t planes, bpp;
    uint32_t compression, img_size;
    int32_t  xppm, yppm;
    uint32_t clr_used, clr_important;
} bmp_ihdr_t;
static int comp_pid  = -1;
static int store_pid = -1;
static int file_disk = 0;
static void *read_file_fast(const char *path, uint32_t *out_size, int *shared_out)
{
    message_t req, resp;
    memset(&req, 0, sizeof(req));
    req.type  = MSG_FILE_OPEN;
    req.data1 = 0xFFFF;
    req.data3 = (uint32_t)file_disk;
    strncpy((char *)req.payload, path, sizeof(req.payload) - 1);
    qk_ipc_send_recv(store_pid, &req, &resp);
    if (resp.type != MSG_FILE_SIZE || resp.data1 == 0)
        return 0;
    uint32_t size        = resp.data1;
    uint32_t page_count  = resp.data2;
    uint32_t page_offset = resp.data3;
    *out_size = size;
    if (page_count > 0 && page_count <= 4096) {
        static uint32_t page_list[4096];
        uint32_t fetched = page_count < 128 ? page_count : 128;
        memcpy(page_list, resp.payload, fetched * sizeof(uint32_t));
        while (fetched < page_count) {
            memset(&req, 0, sizeof(req));
            req.type  = MSG_FILE_GET_PAGES;
            req.data1 = fetched;
            qk_ipc_send_recv(store_pid, &req, &resp);
            if (resp.type != MSG_FILE_SIZE || resp.data1 == 0) break;
            uint32_t got = resp.data1;
            if (fetched + got > 4096) got = 4096 - fetched;
            memcpy(page_list + fetched, resp.payload, got * sizeof(uint32_t));
            fetched += got;
        }
        if (fetched == page_count) {
            uint32_t virt = qk_map_phys_list(page_list, page_count);
            if (virt) {
                if (shared_out) *shared_out = 1;
                return (void *)(virt + page_offset);
            }
        }
    }
    if (shared_out) *shared_out = 0;
    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf) return 0;
    uint32_t off = 0;
    while (off < size) {
        memset(&req, 0, sizeof(req));
        req.type  = MSG_REQ_DATA;
        req.data1 = off;
        req.data2 = 504;
        qk_ipc_send_recv(store_pid, &req, &resp);
        if (resp.type != MSG_FILE_DATA) break;
        uint32_t got = resp.data1;
        if (got == 0) break;
        memcpy(buf + off, resp.payload, got);
        off += got;
    }
    return buf;
}
static int char_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}
typedef enum { FMT_BMP, FMT_PNG, FMT_JPEG, FMT_GIF, FMT_UNKNOWN } img_fmt_t;
static img_fmt_t detect_format(const char *path)
{
    const char *dot = 0;
    for (const char *p = path; *p; p++)
        if (*p == '.') dot = p;
    if (!dot) return FMT_UNKNOWN;
    const char *ext = dot + 1;
    char low[5] = {0};
    for (int i = 0; i < 4 && ext[i]; i++)
        low[i] = (char)char_lower((unsigned char)ext[i]);
    if (low[0]=='b' && low[1]=='m' && low[2]=='p')              return FMT_BMP;
    if (low[0]=='p' && low[1]=='n' && low[2]=='g')              return FMT_PNG;
    if (low[0]=='j' && low[1]=='p' && low[2]=='g')              return FMT_JPEG;
    if (low[0]=='j' && low[1]=='p' && low[2]=='e' && low[3]=='g') return FMT_JPEG;
    if (low[0]=='g' && low[1]=='i' && low[2]=='f')              return FMT_GIF;
    return FMT_UNKNOWN;
}
static void decode_bmp_to_buf(uint32_t *gfx_buf, int buf_w, int buf_h,
                               void *bmp_data, uint32_t bmp_size)
{
    if (bmp_size < sizeof(bmp_fhdr_t) + sizeof(bmp_ihdr_t))
        return;
    bmp_fhdr_t *fhdr = (bmp_fhdr_t *)bmp_data;
    if (fhdr->sig[0] != 'B' || fhdr->sig[1] != 'M')
        return;
    bmp_ihdr_t *ihdr = (bmp_ihdr_t *)((uint8_t *)bmp_data + sizeof(bmp_fhdr_t));
    if (ihdr->bpp != 24 && ihdr->bpp != 32)
        return;
    int src_w   = ihdr->width;
    int src_h   = ihdr->height < 0 ? -ihdr->height : ihdr->height;
    int flipped = (ihdr->height > 0);
    int bpp     = ihdr->bpp;
    int stride  = (src_w * (bpp / 8) + 3) & ~3;
    uint8_t *pixels = (uint8_t *)bmp_data + fhdr->pix_offset;
    int copy_w = src_w < buf_w ? src_w : buf_w;
    int copy_h = src_h < buf_h ? src_h : buf_h;
    for (int dy = 0; dy < copy_h; dy++) {
        int sy = flipped ? (src_h - 1 - dy) : dy;
        uint8_t  *src_row = pixels + (uint32_t)sy * (uint32_t)stride;
        uint32_t *dst_row = gfx_buf + dy * buf_w;
        if (bpp == 32) {
            for (int dx = 0; dx < copy_w; dx++) {
                uint8_t b = src_row[dx * 4 + 0];
                uint8_t g = src_row[dx * 4 + 1];
                uint8_t r = src_row[dx * 4 + 2];
                dst_row[dx] = 0xFF000000u | ((uint32_t)r << 16) |
                              ((uint32_t)g << 8) | b;
            }
        } else {
            for (int dx = 0; dx < copy_w; dx++) {
                uint8_t b = src_row[dx * 3 + 0];
                uint8_t g = src_row[dx * 3 + 1];
                uint8_t r = src_row[dx * 3 + 2];
                dst_row[dx] = 0xFF000000u | ((uint32_t)r << 16) |
                              ((uint32_t)g << 8) | b;
            }
        }
    }
}
static void blit_rgba_to_buf(uint32_t *gfx_buf, int buf_w, int buf_h,
                              uint8_t *rgba, int img_w, int img_h)
{
    int copy_w = img_w < buf_w ? img_w : buf_w;
    int copy_h = img_h < buf_h ? img_h : buf_h;
    uint32_t *px = (uint32_t *)rgba;
    int total = img_w * img_h;
    for (int i = 0; i < total; i++) {
        uint32_t p = px[i];
        uint8_t r = (uint8_t)(p);
        uint8_t g = (uint8_t)(p >> 8);
        uint8_t b = (uint8_t)(p >> 16);
        uint8_t a = (uint8_t)(p >> 24);
        px[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
    if (copy_w == img_w && copy_w == buf_w) {
        memcpy32(gfx_buf, rgba, (uint32_t)copy_w * (uint32_t)copy_h * 4);
    } else {
        for (int dy = 0; dy < copy_h; dy++)
            memcpy32(gfx_buf + dy * buf_w, px + dy * img_w,
                     (uint32_t)copy_w * 4);
    }
}
static void win_print_at(int x, int y, uint32_t color, const char *s)
{
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type  = MSG_WIN_PRINT_AT;
    msg.data1 = (uint32_t)x;
    msg.data2 = (uint32_t)y;
    msg.data3 = color;
    strncpy((char *)msg.payload, s, sizeof(msg.payload) - 1);
    qk_sys_ipc_send(comp_pid, &msg);
}
int main(void)
{
    qk_set_proc_name(-1, "imgview");
    static char file_path[128];
    strncpy(file_path, "/SYS/DAT/LOGO.BMP", sizeof(file_path) - 1);
    int retries = 0;
    while ((comp_pid  = qk_get_server_pid(QK_SERVER_COMPOSITOR)) < 0 ||
           (store_pid = qk_get_server_pid(QK_SERVER_STORAGE))    < 0) {
        qk_sleep(50);
        if (++retries > 40) return 1;
    }
    {
        message_t probe;
        while (qk_ipc_recv_nb(&probe) == 0) {
            if (probe.type == MSG_FILE_OPEN && probe.payload[0] != '\0') {
                strncpy(file_path, (char *)probe.payload, sizeof(file_path) - 1);
                file_path[sizeof(file_path) - 1] = '\0';
                file_disk = (int)probe.data3;
                break;
            }
        }
    }
    uint32_t t0 = qk_get_uptime();
    uint32_t file_size = 0;
    int      used_shm  = 0;
    void    *file_data = read_file_fast(file_path, &file_size, &used_shm);
    uint32_t load_ms   = qk_get_uptime() - t0;
    img_fmt_t fmt   = detect_format(file_path);
    int       img_w = 0;
    int       img_h = 0;
    uint8_t  *rgba  = 0;
    if (file_data && file_size > 0) {
        if (fmt == FMT_BMP) {
            if (file_size >= sizeof(bmp_fhdr_t) + sizeof(bmp_ihdr_t)) {
                bmp_fhdr_t *fh = (bmp_fhdr_t *)file_data;
                if (fh->sig[0] == 'B' && fh->sig[1] == 'M') {
                    bmp_ihdr_t *ih = (bmp_ihdr_t *)((uint8_t *)file_data + sizeof(bmp_fhdr_t));
                    img_w = ih->width;
                    img_h = ih->height < 0 ? -ih->height : ih->height;
                }
            }
        } else {
            int ch = 0;
            stbi_info_from_memory(
                (const stbi_uc *)file_data, (int)file_size,
                &img_w, &img_h, &ch);
        }
    }
    int win_w = (img_w > 0 && img_w <= 4096) ? img_w : WIN_W;
    int win_h = (img_h > 0 && img_h <= 4096) ? img_h : WIN_H;
    const char *title = file_path;
    for (const char *p = file_path; *p; p++)
        if (*p == '/' || *p == '\\') title = p + 1;
    message_t msg, resp;
    memset(&msg, 0, sizeof(msg));
    msg.type  = MSG_WIN_CREATE;
    msg.data1 = (uint32_t)win_w;
    msg.data2 = (uint32_t)win_h;
    strncpy((char *)msg.payload, title, sizeof(msg.payload) - 1);
    qk_sys_ipc_send(comp_pid, &msg);
    while (1) { qk_ipc_recv(&resp); if (resp.type == MSG_WIN_CREATED) break; }
    int win_id = (int)resp.data1;
    int gfx_w = 0, gfx_h = 0;
    uint32_t *gfx = (uint32_t *)qk_win_map_buffer(comp_pid, win_id, &gfx_w, &gfx_h);
    uint32_t decode_ms = 0;
    if (file_data && file_size > 0) {
        if (fmt == FMT_BMP) {
        } else {
            uint32_t td = qk_get_uptime();
            int channels = 0;
            rgba = stbi_load_from_memory(
                (const stbi_uc *)file_data, (int)file_size,
                &img_w, &img_h, &channels, 4);
            decode_ms = qk_get_uptime() - td;
        }
    }
    if (gfx) {
        memset32(gfx, 0xFF1E2127u, (uint32_t)gfx_w * (uint32_t)gfx_h);
        if (fmt == FMT_BMP && file_data)
            decode_bmp_to_buf(gfx, gfx_w, gfx_h, file_data, file_size);
        else if (rgba)
            blit_rgba_to_buf(gfx, gfx_w, gfx_h, rgba, img_w, img_h);
        else if (!file_data)
            win_print_at(4, 4, 0xFFFF4444, "could not load file");
        else
            win_print_at(4, 4, 0xFFFF4444, "unsupported format");
        {
            static char tbuf[64];
            uint32_t i = 0;
            tbuf[i++] = 'L'; tbuf[i++] = ':';
            uint32_t v = load_ms;
            if (v == 0) { tbuf[i++] = '0'; } else {
                char tmp[10]; int ti = 0;
                while (v) { tmp[ti++] = '0' + (v % 10); v /= 10; }
                while (ti--) tbuf[i++] = tmp[ti+1];
            }
            tbuf[i++] = 'm'; tbuf[i++] = 's'; tbuf[i++] = ' ';
            tbuf[i++] = 'D'; tbuf[i++] = ':';
            v = decode_ms;
            if (v == 0) { tbuf[i++] = '0'; } else {
                char tmp[10]; int ti = 0;
                while (v) { tmp[ti++] = '0' + (v % 10); v /= 10; }
                while (ti--) tbuf[i++] = tmp[ti+1];
            }
            tbuf[i++] = 'm'; tbuf[i++] = 's'; tbuf[i] = '\0';
            win_print_at(2, 2, 0xFFFFFF00, tbuf);
        }
        qk_win_dirty(comp_pid);
    } else {
        win_print_at(4, 4, 0xFFAAAAAA, "shared buf unavail");
    }
    while (1) {
        message_t ev;
        while (qk_ipc_recv_nb(&ev) == 0) {
            if (ev.type == MSG_WIN_RESIZED) {
                int new_gfx_w = 0, new_gfx_h = 0;
                uint32_t *new_gfx = (uint32_t *)qk_win_map_buffer(
                    comp_pid, win_id, &new_gfx_w, &new_gfx_h);
                if (new_gfx) {
                    gfx   = new_gfx;
                    gfx_w = new_gfx_w;
                    gfx_h = new_gfx_h;
                    memset32(gfx, 0xFF1E2127u, (uint32_t)gfx_w * (uint32_t)gfx_h);
                    if (fmt == FMT_BMP && file_data)
                        decode_bmp_to_buf(gfx, gfx_w, gfx_h, file_data, file_size);
                    else if (rgba)
                        blit_rgba_to_buf(gfx, gfx_w, gfx_h, rgba, img_w, img_h);
                    qk_win_dirty(comp_pid);
                }
            }
        }
        qk_yield();
    }
    return 0;
}
