

#include "kernel/ipc.h"
#include "lib/stdlib.h"
#include "user/lib/font_atlas.h"
#include "user/lib/libapi.h"

#define WIN_W 400
#define WIN_H 300

static int comp_pid  = -1;
static int store_pid = -1;

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

static void *read_file_fast(const char *path, uint32_t *out_size, int *shared_out)
{
    message_t req, resp;
    memset(&req, 0, sizeof(req));
    req.type  = MSG_FILE_OPEN;
    req.data1 = 0xFFFF; 
    strncpy((char *)req.payload, path, sizeof(req.payload) - 1);
    qk_ipc_send_recv(store_pid, &req, &resp);

    if (resp.type != MSG_FILE_SIZE || resp.data1 == 0)
        return 0;

    uint32_t size        = resp.data1;
    uint32_t page_count  = resp.data2;
    uint32_t page_offset = resp.data3;
    *out_size = size;

    
    if (page_count > 0 && page_count <= 128) {
        uint32_t virt = qk_map_phys_list((uint32_t *)resp.payload, page_count);
        if (virt) {
            if (shared_out) *shared_out = 1;
            return (void *)(virt + page_offset);
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

    int src_w    = ihdr->width;
    int src_h    = ihdr->height < 0 ? -ihdr->height : ihdr->height;
    int flipped  = (ihdr->height > 0);
    int bpp      = ihdr->bpp;
    int stride   = (src_w * (bpp / 8) + 3) & ~3;

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
    strncpy(file_path, "/SYSTEM/DATA/LOGO.BMP", sizeof(file_path) - 1);
    {
        message_t probe;
        while (qk_ipc_recv_nb(&probe) == 0) {
            if (probe.type == MSG_FILE_OPEN && probe.payload[0] != '\0') {
                strncpy(file_path, (char *)probe.payload, sizeof(file_path) - 1);
                file_path[sizeof(file_path) - 1] = '\0';
                break;
            }
        }
    }

    
    int retries = 0;
    while ((comp_pid  = qk_get_server_pid(QK_SERVER_COMPOSITOR)) < 0 ||
           (store_pid = qk_get_server_pid(QK_SERVER_STORAGE))    < 0) {
        qk_sleep(50);
        if (++retries > 40) return 1;
    }

    
    uint32_t t0 = qk_get_uptime();
    uint32_t bmp_size = 0;
    int      used_shm = 0;
    void    *bmp_data = read_file_fast(file_path, &bmp_size, &used_shm);
    uint32_t t1 = qk_get_uptime();
    uint32_t load_ms = t1 - t0;

    
    int win_w = WIN_W;
    int win_h = WIN_H;
    if (bmp_data && bmp_size >= sizeof(bmp_fhdr_t) + sizeof(bmp_ihdr_t)) {
        bmp_fhdr_t *fhdr = (bmp_fhdr_t *)bmp_data;
        if (fhdr->sig[0] == 'B' && fhdr->sig[1] == 'M') {
            bmp_ihdr_t *ihdr = (bmp_ihdr_t *)((uint8_t *)bmp_data + sizeof(bmp_fhdr_t));
            int iw = ihdr->width;
            int ih = ihdr->height < 0 ? -ihdr->height : ihdr->height;
            if (iw > 0 && iw <= 1920) win_w = iw;
            if (ih > 0 && ih <= 1080) win_h = ih;
        }
    }

    
    message_t msg, resp;
    memset(&msg, 0, sizeof(msg));
    msg.type  = MSG_WIN_CREATE;
    msg.data1 = (uint32_t)win_w;
    msg.data2 = (uint32_t)win_h;
    
    const char *title = file_path;
    for (const char *p = file_path; *p; p++)
        if (*p == '/' || *p == '\\') title = p + 1;
    {
        char titlebuf[64];
        int ti = 0;
        for (const char *p = title; *p && ti < 48; p++) titlebuf[ti++] = *p;
        titlebuf[ti++] = ' '; titlebuf[ti++] = '(';
        ti += int_to_str((int)load_ms, titlebuf + ti, 0);
        titlebuf[ti++] = 'm'; titlebuf[ti++] = 's'; titlebuf[ti++] = ')';
        titlebuf[ti] = '\0';
        strncpy((char *)msg.payload, titlebuf, 64);
    }
    qk_sys_ipc_send(comp_pid, &msg);

    while (1) { qk_ipc_recv(&resp); if (resp.type == MSG_WIN_CREATED) break; }
    int win_id = (int)resp.data1;

    
    int gfx_w = 0, gfx_h = 0;
    uint32_t *gfx = (uint32_t *)qk_win_map_buffer(comp_pid, win_id, &gfx_w, &gfx_h);

    if (gfx && bmp_data) {
        
        memset32(gfx, 0xFF1E2127u, (uint32_t)gfx_w * (uint32_t)gfx_h);

        
        decode_bmp_to_buf(gfx, gfx_w, gfx_h, bmp_data, bmp_size);

        
        qk_win_dirty(comp_pid);
    } else if (!gfx) {
        
        win_print_at(4, 4, 0xFFAAAAAA, "shared buf unavail, using blit...");
        if (bmp_data) {
            bmp_ihdr_t *ihdr = (bmp_ihdr_t *)((uint8_t *)bmp_data + sizeof(bmp_fhdr_t));
            int src_w   = ihdr->width;
            int src_h   = ihdr->height < 0 ? -ihdr->height : ihdr->height;
            int bpp_fb  = (ihdr->bpp == 32) ? 4 : 3;
            int row_pad = (src_w * bpp_fb + 3) & ~3;
            int flipped = (ihdr->height > 0);
            uint8_t *pixels = (uint8_t *)bmp_data + ((bmp_fhdr_t *)bmp_data)->pix_offset;
            int copy_w = src_w < WIN_W ? src_w : WIN_W - 4;
            int copy_h = src_h < (WIN_H - 40) ? src_h : (WIN_H - 40);
            uint32_t row_buf[128];
            for (int dy = 0; dy < copy_h; dy++) {
                int sy = flipped ? (src_h - 1 - dy) : dy;
                uint8_t *src_row = pixels + (uint32_t)sy * (uint32_t)row_pad;
                int dx = 0;
                while (dx < copy_w) {
                    int batch = copy_w - dx;
                    if (batch > 128) batch = 128;
                    for (int p = 0; p < batch; p++) {
                        uint8_t b = src_row[(dx + p) * bpp_fb + 0];
                        uint8_t g = src_row[(dx + p) * bpp_fb + 1];
                        uint8_t r = src_row[(dx + p) * bpp_fb + 2];
                        row_buf[p] = 0xFF000000u | ((uint32_t)r << 16) |
                                     ((uint32_t)g << 8) | b;
                    }
                    qk_blit_pixels(comp_pid, dx, 30 + dy, row_buf, batch);
                    dx += batch;
                }
            }
        }
    }

    
    while (1) {
        message_t ev;
        while (qk_ipc_recv_nb(&ev) == 0) {  }
        qk_yield();
    }

    return 0;
}
