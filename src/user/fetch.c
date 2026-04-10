#include "kernel/ipc.h"
#include "lib/stdlib.h"
#include "user/lib/libapi.h"
#include "user/lib/path.h"
#include <stddef.h>

#define WIN_W   500
#define WIN_H   188

#define LOGO_DST_W  96
#define LOGO_DST_H  96
#define LOGO_OFF_X  12
#define LOGO_OFF_Y  ((WIN_H - LOGO_DST_H) / 2)

#define INFO_X  124
#define INFO_Y_START  8
#define INFO_LINE_H   18

#define C_BG        0xFF1E2127
#define C_HEADER    0xFFE5C07B
#define C_KEY       0xFF61AFEF
#define C_VAL       0xFFABB2BF
#define C_ACCENT    0xFF98C379
#define C_DIM       0xFF5C6370
#define C_RED       0xFFE06C75
#define C_PURPLE    0xFFC678DD

static int comp_pid  = -1;
static int store_pid = -1;

typedef struct __attribute__((packed)) {
    uint8_t  sig[2];
    uint32_t file_size;
    uint16_t reserved1, reserved2;
    uint32_t pix_offset;
} bmp_fhdr_t;

typedef struct __attribute__((packed)) {
    uint32_t hdr_size;
    int32_t  width, height;
    uint16_t planes, bpp;
    uint32_t compression, img_size;
    int32_t  x_ppm, y_ppm;
    uint32_t clr_used, clr_important;
} bmp_ihdr_t;

static void *fetch_read_file(const char *path, uint32_t *out_size)
{
    uint16_t dir_cluster;
    char name[16];
    if (qk_path_resolve(path, &dir_cluster, name) != 0)
        return 0;

    message_t req, resp;
    memset(&req, 0, sizeof(req));
    req.type  = MSG_FILE_OPEN;
    req.data1 = dir_cluster;
    strncpy((char *)req.payload, name, 12);
    qk_sys_ipc_send(store_pid, &req);

    while (1) { qk_ipc_recv(&resp); if (resp.sender_pid == store_pid) break; }
    if (resp.type != MSG_FILE_SIZE) return 0;

    uint32_t size = resp.data1;
    *out_size = size;
    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf) return 0;

    uint32_t off = 0;
    while (off < size) {
        memset(&req, 0, sizeof(req));
        req.type  = MSG_REQ_DATA;
        req.data1 = off;
        req.data2 = 100;
        qk_sys_ipc_send(store_pid, &req);
        while (1) {
            qk_ipc_recv(&resp);
            if (resp.sender_pid == store_pid && resp.type == MSG_FILE_DATA) break;
        }
        uint32_t got = resp.data1;
        if (got == 0) break;
        memcpy(buf + off, resp.payload, got);
        off += got;
    }
    return buf;
}

static void draw_rect(int x, int y, int w, int h, uint32_t color)
{
    message_t msg;
    msg.type  = MSG_WIN_DRAW_RECT;
    msg.data1 = y;
    msg.data2 = x;
    msg.data3 = w;
    *(int *)     &msg.payload[0] = h;
    *(uint32_t *)&msg.payload[4] = color;
    qk_sys_ipc_send(comp_pid, &msg);
}

static void blit_logo(void *bmp_data, uint32_t bmp_size)
{
    if (!bmp_data || bmp_size < sizeof(bmp_fhdr_t) + sizeof(bmp_ihdr_t))
        return;

    bmp_fhdr_t *fh = (bmp_fhdr_t *)bmp_data;
    bmp_ihdr_t *ih = (bmp_ihdr_t *)((uint8_t *)bmp_data + sizeof(bmp_fhdr_t));

    if (fh->sig[0] != 'B' || fh->sig[1] != 'M') return;

    int src_w   = ih->width;
    int src_h   = (ih->height < 0) ? -ih->height : ih->height;
    int bpp     = ih->bpp;
    int Bpp     = bpp / 8;
    int stride  = ((src_w * bpp + 31) / 32) * 4;
    uint8_t *px = (uint8_t *)bmp_data + fh->pix_offset;

    int dst_w = LOGO_DST_W;
    int dst_h = LOGO_DST_H;
    int ox    = LOGO_OFF_X;
    int oy    = LOGO_OFF_Y;

    
    uint32_t row_buf[30];
    int row_buf_x = 0;
    int row_buf_cnt = 0;

    for (int dy = 0; dy < dst_h; dy++) {
        

        int sy_fwd = (dy * src_h) / dst_h;           
        int sy_bmp = (ih->height > 0)
                       ? (src_h - 1 - sy_fwd)        
                       : sy_fwd;                      
        uint8_t *src_row = px + sy_bmp * stride;

        row_buf_cnt = 0;
        row_buf_x   = ox;

        for (int dx = 0; dx < dst_w; dx++) {
            int sx = (dx * src_w) / dst_w;
            uint8_t *p = src_row + sx * Bpp;
            uint8_t b = p[0], g = p[1], r = p[2];
            uint8_t a = (bpp == 32) ? p[3] : 255;
            uint32_t col = (0xFF000000u) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            if (a < 128) col = C_BG;   

            row_buf[row_buf_cnt++] = col;

            if (row_buf_cnt == 30 || dx == dst_w - 1) {
                qk_blit_pixels(comp_pid, row_buf_x, oy + dy, row_buf, row_buf_cnt);
                row_buf_x   += row_buf_cnt;
                row_buf_cnt  = 0;
            }
        }
    }
}

static int info_y = INFO_Y_START;

static void info_line(const char *key, const char *val, uint32_t val_color)
{
    qk_print_at(INFO_X,      info_y, C_KEY,   key);
    qk_print_at(INFO_X + 76, info_y, C_DIM,   ": ");
    qk_print_at(INFO_X + 90, info_y, val_color, val);
    info_y += INFO_LINE_H;
}

static void u32_str(uint32_t v, char *buf)   { int_to_str(v, buf, 0); }

static void fmt_uptime(uint32_t ms, char *buf)
{
    uint32_t s = ms / 1000;
    uint32_t h = s / 3600, m = (s % 3600) / 60;
    s = s % 60;
    char tmp[8];
    u32_str(h, buf);  strcat(buf, "h ");
    u32_str(m, tmp);  strcat(buf, tmp); strcat(buf, "m ");
    u32_str(s, tmp);  strcat(buf, tmp); strcat(buf, "s");
}

static void fmt_mem(uint32_t bytes, char *buf)
{
    u32_str(bytes / 1024 / 1024, buf);
    strcat(buf, " MB");
}

static void fmt_date(qk_time_t *t, char *buf)
{
    char tmp[8];
    u32_str(t->year,  buf);  strcat(buf, "-");
    u32_str(t->month, tmp);  strcat(buf, tmp); strcat(buf, "-");
    u32_str(t->day,   tmp);  strcat(buf, tmp);
    strcat(buf, "  ");
    u32_str(t->hour,  tmp);  strcat(buf, tmp); strcat(buf, ":");
    u32_str(t->minute,tmp);  strcat(buf, tmp);
}

int main(void)
{
    store_pid = qk_get_server_pid(QK_SERVER_STORAGE);
    comp_pid  = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    if (store_pid < 0 || comp_pid < 0) return 1;

    
    message_t msg;
    msg.type  = MSG_WIN_CREATE;
    msg.data1 = WIN_W;
    msg.data2 = WIN_H;
    strncpy((char *)msg.payload, "fetch", 127);
    qk_sys_ipc_send(comp_pid, &msg);

    message_t resp;
    while (1) { qk_ipc_recv(&resp); if (resp.type == MSG_WIN_CREATED) break; }

    
    draw_rect(0, 0, WIN_W, WIN_H, C_BG);

    
    draw_rect(0, 0, 3, WIN_H, C_PURPLE);

    
    draw_rect(LOGO_OFF_X + LOGO_DST_W + 12, 8,
              1, WIN_H - 16, C_DIM);

    
    uint32_t bmp_size = 0;
    void *bmp = fetch_read_file("/SYSTEM/DATA/LOGO.BMP", &bmp_size);
    if (bmp) {
        blit_logo(bmp, bmp_size);
        free(bmp);
    } else {
        
        draw_rect(LOGO_OFF_X, LOGO_OFF_Y, LOGO_DST_W, LOGO_DST_H, 0xFF282C34);
        qk_print_at(LOGO_OFF_X + 28, LOGO_OFF_Y + 42, C_DIM, "no logo");
    }

    
    uint32_t used_mem = 0, total_mem = 0, cpu_load = 0;
    qk_get_stats(&used_mem, &total_mem, &cpu_load);

    process_info_t procs[32];
    int nprocs = qk_get_procs(procs, 32);
    if (nprocs < 0) nprocs = 0;

    qk_time_t t;
    qk_get_time(&t);

    char uptime_s[32]  = {0};
    char used_s[16]    = {0};
    char total_s[16]   = {0};
    char cpu_s[12]     = {0};
    char procs_s[8]    = {0};
    char date_s[32]    = {0};
    char mem_s[40]     = {0};

    fmt_uptime(qk_get_uptime(), uptime_s);
    fmt_mem(used_mem,  used_s);
    fmt_mem(total_mem, total_s);
    u32_str(cpu_load, cpu_s);  strcat(cpu_s, "%");
    u32_str((uint32_t)nprocs, procs_s);
    fmt_date(&t, date_s);

    strncpy(mem_s, used_s, 38);
    strcat(mem_s, " / ");
    strcat(mem_s, total_s);

    
    qk_print_at(INFO_X, info_y, C_ACCENT,  "user");
    qk_print_at(INFO_X + 28, info_y, C_DIM, "@");
    qk_print_at(INFO_X + 38, info_y, C_HEADER, "QuarkOS32");
    info_y += INFO_LINE_H;

    
    draw_rect(INFO_X, info_y, WIN_W - INFO_X - 10, 1, C_DIM);
    info_y += 8;

    info_line("OS",     "QuarkOS32 Keystone",  C_VAL);
    info_line("Kernel", "QK32-1.1",            C_VAL);
    info_line("Arch",   "x86 (i686)",          C_VAL);
    info_line("Uptime", uptime_s,              C_ACCENT);
    info_line("Memory", mem_s,                 C_RED);
    info_line("CPU",    cpu_s,                 C_RED);
    info_line("Procs",  procs_s,               C_PURPLE);
    info_line("Date",   date_s,                C_DIM);
    info_line("Shell",  "quartz",              C_VAL);

    
    info_y += 4;
    uint32_t swatches[] = { 0xFFE06C75, 0xFFE5C07B, 0xFF98C379,
                             0xFF61AFEF, 0xFFC678DD, 0xFF56B6C2,
                             0xFFABB2BF, 0xFF5C6370 };
    for (int i = 0; i < 8; i++)
        draw_rect(INFO_X + i * 16, info_y, 14, 10, swatches[i]);

    
    while (1) {
        qk_ipc_recv(&msg);
        if (msg.type == MSG_PROCESS_TERMINATED)
            break;
    }

    return 0;
}
