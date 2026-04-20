/*
 * terminal.c – terminal emulator for QuarkOS
 *
 * Supports a subset of ANSI SGR escape sequences for color output.
 * Spawns ttysh as a child process and relays I/O.
 */
#include "gfx/events.h"
#include "kernel/ipc.h"
#include "lib/stdbool.h"
#include "lib/stdlib.h"
#include "user/lib/font_atlas.h"
#include "user/lib/gfx.h"
#include "user/lib/libapi.h"
#include "user/lib/storage_client.h"
#include "user/lib/window.h"
#include <stddef.h>

/* ── background ─────────────────────────────────────────────────────────── */
#define COL_BG        0xFF1E1E1Eu
#define COL_CURSOR    0xFF888888u

/* ── ANSI 16-color palette ───────────────────────────────────────────────── */
static const uint32_t ansi_pal[16] = {
    0xFFAAAAAAu,  /* 0  default text */
    0xFF555555u,  /* 1  dim / dark gray */
    0xFFCC6666u,  /* 2  red */
    0xFF88AA88u,  /* 3  green */
    0xFFCCAA66u,  /* 4  yellow */
    0xFF7799AAu,  /* 5  blue */
    0xFF88AABBu,  /* 6  cyan */
    0xFFCCCCCCu,  /* 7  bright white */
    0xFFAA5555u,  /* 8  dark red */
    0xFF99BB99u,  /* 9  bright green */
    0xFFDDBB88u,  /* 10 bright yellow */
    0xFF88AABBu,  /* 11 bright blue */
    0xFF99BBCCu,  /* 12 bright cyan */
    0xFFFFFFFFu,  /* 13 white */
    0xFF666666u,  /* 14 dim */
    0xFFCCCCCCu,  /* 15 default */
};

/* ── limits ─────────────────────────────────────────────────────────────── */
#define MAX_COLS     256
#define MAX_LINES    800
#define PAD_X         10
#define PAD_Y          4

/* ── line ring buffer (char + color index per cell) ─────────────────────── */
static char    lbuf[MAX_LINES][MAX_COLS];
static uint8_t lcol[MAX_LINES][MAX_COLS];
static int     ltotal   = 0;
static char    lcur[MAX_COLS];
static uint8_t lcur_col[MAX_COLS];
static int     lcur_len = 0;

static int lbuf_idx(int n) { return n % MAX_LINES; }

static void lb_commit(void)
{
    lcur[lcur_len] = 0;
    int idx  = lbuf_idx(ltotal);
    int copy = lcur_len < MAX_COLS-1 ? lcur_len : MAX_COLS-1;
    memcpy(lbuf[idx], lcur,     copy);
    memcpy(lcol[idx], lcur_col, copy);
    lbuf[idx][copy] = 0;
    ltotal++;
    lcur_len = 0;
}

/* ── ANSI escape parser ─────────────────────────────────────────────────── */
static int     esc_state   = 0;   /* 0=normal, 1=saw ESC, 2=in CSI */
static char    esc_buf[16];
static int     esc_len     = 0;
static uint8_t cur_col_idx = 0;   /* current foreground color index */

static void ansi_sgr(void)
{
    int pos = 0;
    while (pos <= esc_len) {
        int n = 0, got = 0;
        while (pos < esc_len && esc_buf[pos] >= '0' && esc_buf[pos] <= '9') {
            n = n * 10 + (esc_buf[pos++] - '0'); got = 1;
        }
        if (!got) break;
        if (pos < esc_len && esc_buf[pos] == ';') pos++;
        else pos = esc_len;

        if      (n == 0)  cur_col_idx = 0;
        else if (n == 1)  cur_col_idx = 7;   /* bold → bright white */
        else if (n == 2)  cur_col_idx = 1;   /* dim */
        else if (n == 30) cur_col_idx = 1;
        else if (n == 31) cur_col_idx = 2;
        else if (n == 32) cur_col_idx = 3;
        else if (n == 33) cur_col_idx = 4;
        else if (n == 34) cur_col_idx = 5;
        else if (n == 35) cur_col_idx = 2;
        else if (n == 36) cur_col_idx = 6;
        else if (n == 37) cur_col_idx = 0;
        else if (n == 90) cur_col_idx = 1;
        else if (n == 91) cur_col_idx = 8;
        else if (n == 92) cur_col_idx = 9;
        else if (n == 93) cur_col_idx = 10;
        else if (n == 94) cur_col_idx = 11;
        else if (n == 95) cur_col_idx = 8;
        else if (n == 96) cur_col_idx = 12;
        else if (n == 97) cur_col_idx = 7;
    }
}

static void term_put(char c)
{
    if (esc_state == 1) {
        if (c == '[') { esc_state = 2; esc_len = 0; return; }
        esc_state = 0;
    } else if (esc_state == 2) {
        if (c == 'm') {
            esc_buf[esc_len] = 0;
            ansi_sgr();
            esc_state = 0;
        } else if (esc_len < 15) {
            esc_buf[esc_len++] = c;
        } else {
            esc_state = 0;
        }
        return;
    }

    if (c == '\033') { esc_state = 1; return; }

    if (c == '\n') {
        lb_commit();
    } else if (c == '\b') {
        if (lcur_len > 0) lcur_len--;
    } else if (c == '\r') {
        /* ignore */
    } else {
        if (lcur_len < MAX_COLS-1) {
            lcur[lcur_len]     = c;
            lcur_col[lcur_len] = cur_col_idx;
            lcur_len++;
        }
    }
}

static void term_put_str(const char *s)
{
    while (*s) term_put(*s++);
}

/* ── display ─────────────────────────────────────────────────────────────── */
static int       comp_pid  = -1;
static int       ttysh_pid = -1;
static uint32_t *gfx       = 0;
static int       gfx_w     = 0;
static int       gfx_h     = 0;
static int       win_id    = -1;
static int       cell_h    = 16;
static int       vis_rows    = 20;
static int       output_rows = 19;
static int       scroll_up   = 0;

/* ── helpers ─────────────────────────────────────────────────────────────── */
static void fill_rect(int x, int y, int w, int h, uint32_t c)
{
    if (w<=0||h<=0) return;
    if (x<0){w+=x;x=0;} if (y<0){h+=y;y=0;}
    if (x+w>gfx_w) w=gfx_w-x; if (y+h>gfx_h) h=gfx_h-y;
    if (w<=0||h<=0) return;
    for (int row=0;row<h;row++) {
        uint32_t *p=gfx+(y+row)*gfx_w+x;
        for (int k=0;k<w;k++) p[k]=c;
    }
}

/* Draw a line with per-character color indices; returns final x. */
static int draw_colored_line(int x, int y, const char *str,
                              const uint8_t *cols, int len)
{
    gfx_context_t ctx = {gfx,(uint32_t)gfx_w,(uint32_t)gfx_h,(uint32_t)gfx_w};
    for (int i = 0; i < len && str[i]; i++) {
        uint32_t color = ansi_pal[cols[i] & 0xF];
        x = draw_codepoint_vector(&ctx, x, y, (uint8_t)str[i], color);
    }
    return x;
}

/* ── render ─────────────────────────────────────────────────────────────── */
static void term_render(void)
{
    if (!gfx) return;

    int panel_h = cell_h + PAD_Y * 4;
    int gap     = PAD_Y * 2;
    int out_area = gfx_h - panel_h - gap - PAD_Y;
    output_rows = out_area / (cell_h + 1);
    if (output_rows < 0) output_rows = 0;
    vis_rows = output_rows + 1;

    fill_rect(0, 0, gfx_w, gfx_h, COL_BG);

    /* scrollback: bottom-anchored so newest output sits just above input */
    int first = ltotal - output_rows - scroll_up;
    if (first < 0) first = 0;
    int visible = ltotal - first;
    int start_row = output_rows - visible;
    if (start_row < 0) start_row = 0;

    for (int row = start_row; row < output_rows; row++) {
        int li = first + (row - start_row);
        if (li < 0 || li >= ltotal) continue;
        if (ltotal - li > MAX_LINES) continue;
        int y = PAD_Y + row * (cell_h + 1);
        int idx = lbuf_idx(li);
        int len = strlen(lbuf[idx]);
        draw_colored_line(PAD_X, y, lbuf[idx], lcol[idx], len);
    }

    /* pinned input panel at the bottom */
    {
        int ry = gfx_h - panel_h;
        fill_rect(0, ry, gfx_w, panel_h, 0xFF252525u);
        int ty = ry + (panel_h - cell_h) / 2;
        int x = draw_colored_line(PAD_X, ty, lcur, lcur_col, lcur_len);
        int cw = get_char_width('M'); if (cw < 2) cw = 8;
        fill_rect(x, ty, cw, cell_h, COL_CURSOR);
    }

    qk_win_dirty(comp_pid);
}

/* ── ttysh spawn ─────────────────────────────────────────────────────────── */
static int spawn_ttysh(void)
{
    int storage = qk_get_server_pid(QK_SERVER_STORAGE);
    if (storage < 0) return -1;

    message_t req; memset(&req,0,sizeof(req));
    req.type=MSG_DIR_REQ; req.data1=0;
    qk_sys_ipc_send(storage,&req);
    uint16_t apps_cluster=0;
    while (1) {
        message_t resp; qk_ipc_recv(&resp);
        if (resp.sender_pid!=storage||resp.type!=MSG_DIR_REPLY) continue;
        char *ptr=(char*)resp.payload;
        while (*ptr) {
            int is_dir=(*ptr=='1');
            while (*ptr&&*ptr!='|') ptr++; if (*ptr=='|') ptr++;
            int cl=0; while (*ptr>='0'&&*ptr<='9') cl=cl*10+(*ptr++-'0');
            uint16_t clus=(uint16_t)cl;
            if (*ptr=='|') ptr++;
            char name[16]; int ni=0;
            while (*ptr&&*ptr!='\n'&&ni<15) name[ni++]=*ptr++;
            name[ni]=0; if (*ptr=='\n') ptr++;
            if (is_dir) {
                const char *n="APPS"; int match=1;
                for (int i=0;i<4&&name[i];i++) {
                    char a=name[i]; if (a>='a'&&a<='z') a-=32;
                    if (a!=n[i]) { match=0; break; }
                }
                if (match) apps_cluster=clus;
            }
        }
        if (resp.data1==0) break;
    }
    if (!apps_cluster) return -1;

    memset(&req,0,sizeof(req));
    req.type=MSG_FILE_OPEN; req.data1=apps_cluster;
    strncpy((char*)req.payload,"TTYSH.QEX",12);
    qk_sys_ipc_send(storage,&req);
    message_t resp;
    while (1) { qk_ipc_recv(&resp); if (resp.sender_pid==storage) break; }
    if (resp.type!=MSG_FILE_SIZE) return -1;
    uint32_t size=resp.data1;
    uint8_t *buf=malloc(size); if (!buf) return -1;
    uint32_t off=0;
    while (off<size) {
        memset(&req,0,sizeof(req)); req.type=MSG_REQ_DATA; req.data1=off; req.data2=480;
        qk_sys_ipc_send(storage,&req);
        while (1) { qk_ipc_recv(&resp); if (resp.sender_pid==storage&&resp.type==MSG_FILE_DATA) break; }
        uint32_t got=resp.data1; if (!got) break;
        memcpy(buf+off,resp.payload,got); off+=got;
    }

    int pid=qk_spawn(buf,size); free(buf);
    if (pid<0) return -1;

    message_t attach; memset(&attach,0,sizeof(attach));
    attach.type  = MSG_TERM_ATTACH;
    attach.data1 = (uint32_t)qk_getpid();
    qk_sys_ipc_send(pid,&attach);
    return pid;
}

/* ═══════════════════════════════════════════════════════════════════════════
   MAIN
   ═══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    comp_pid = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    if (comp_pid < 0) return 1;

    void *font_addr = 0;
    if (qk_map_font_atlas(&font_addr) == 0)
        font_atlas_init((uint8_t *)font_addr);

    cell_h = get_font_height();
    if (cell_h < 8) cell_h = 16;

    message_t msg; memset(&msg,0,sizeof(msg));
    msg.type=MSG_WIN_CREATE; msg.data1=800; msg.data2=520;
    strncpy((char*)msg.payload,"Terminal",63);
    qk_sys_ipc_send(comp_pid,&msg);
    while (1) { qk_ipc_recv(&msg); if (msg.type==MSG_WIN_CREATED) break; }
    win_id = (int)msg.data1;
    gfx = (uint32_t*)qk_win_map_buffer(comp_pid,win_id,&gfx_w,&gfx_h);

    qk_set_proc_name(-1,"terminal");

    ttysh_pid = spawn_ttysh();
    if (ttysh_pid < 0) {
        term_put_str("terminal: failed to spawn ttysh\n");
        term_render();
        while (1) { qk_ipc_recv(&msg); }
    }

    term_render();

    while (1) {
        qk_ipc_recv(&msg);

        if (msg.sender_pid == ttysh_pid && msg.type == MSG_WIN_PRINT) {
            term_put_str((const char*)msg.payload);
            scroll_up = 0;
            term_render();
            continue;
        }

        if (msg.sender_pid == ttysh_pid && msg.type == MSG_WIN_CLEAR) {
            ltotal=0; lcur_len=0; scroll_up=0;
            term_render();
            continue;
        }

        if (msg.type == MSG_WIN_RESIZED) {
            uint32_t *ng=(uint32_t*)qk_win_map_buffer(comp_pid,win_id,&gfx_w,&gfx_h);
            if (ng) gfx=ng;
            term_render();
            continue;
        }

        if (msg.type != MSG_KEY_EVENT) continue;

        uint32_t raw    = msg.data1;
        bool     is_rel = (raw & 0x80) != 0;
        uint8_t  code   = (uint8_t)(raw & 0x7F);
        bool     ext    = (raw & 0x100) != 0;

        static int awaiting_ext = 0;
        if (!ext && code == 0x00) { awaiting_ext = 1; continue; }
        if (is_rel) { awaiting_ext = 0; continue; }

        int is_pgup=0, is_pgdn=0;
        if (awaiting_ext || ext) {
            awaiting_ext = 0;
            if      (code == 0x49) is_pgup = 1;
            else if (code == 0x51) is_pgdn = 1;
            else { if (ttysh_pid > 0) qk_sys_ipc_send(ttysh_pid, &msg); }
        } else {
            if      (code == 0x49) is_pgup = 1;
            else if (code == 0x51) is_pgdn = 1;
        }

        if (is_pgup) {
            scroll_up += vis_rows / 2;
            int max = ltotal + (lcur_len>0?1:0);
            if (scroll_up > max) scroll_up = max;
            term_render(); continue;
        }
        if (is_pgdn) {
            scroll_up -= vis_rows / 2;
            if (scroll_up < 0) scroll_up = 0;
            term_render(); continue;
        }

        if (ttysh_pid > 0) qk_sys_ipc_send(ttysh_pid, &msg);
    }

    return 0;
}
