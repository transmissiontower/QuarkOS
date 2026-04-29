// SPDX-License-Identifier: MIT
/*
 * src/user/terminal.c - ANSI terminal emulator with scrollback and mouse text selection
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

#define COL_BG        0xFF1E1E1Eu
#define COL_CURSOR    0xFF888888u
#define COL_SEL_BG    0xFF264F78u

static const uint32_t ansi_pal[16] = {
    0xFFAAAAAAu,
    0xFF555555u,
    0xFFCC6666u,
    0xFF88AA88u,
    0xFFCCAA66u,
    0xFF7799AAu,
    0xFF88AABBu,
    0xFFCCCCCCu,
    0xFFAA5555u,
    0xFF99BB99u,
    0xFFDDBB88u,
    0xFF88AABBu,
    0xFF99BBCCu,
    0xFFFFFFFFu,
    0xFF666666u,
    0xFFCCCCCCu,
};

#define MAX_COLS     256
#define MAX_LINES    800
#define PAD_X         10
#define PAD_Y          4

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

static int count_content_lines(void)
{
    int count = 0;
    for (int i = ltotal - 1; i >= 0 && ltotal - i <= MAX_LINES; i--) {
        int idx = lbuf_idx(i);
        if (lbuf[idx][0] != 0) count++;
        else break;
    }
    return count;
}

static int     esc_state   = 0;
static char    esc_buf[16];
static int     esc_len     = 0;
static uint8_t cur_col_idx = 0;

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
        else if (n == 1)  cur_col_idx = 7;
        else if (n == 2)  cur_col_idx = 1;
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

static int  sel_line0 = -1, sel_col0 = 0;
static int  sel_line1 = -1, sel_col1 = 0;
static bool term_sel  = false;
static int  vis_map[128];
static int  vis_map_n = 0;

static void clip_set(const char *text, int len)
{
    message_t m; memset(&m, 0, sizeof(m));
    m.type = MSG_CLIP_SET;
    if (len > 4095) len = 4095;
    m.data1 = (uint32_t)len;
    memcpy(m.payload, text, len);
    qk_sys_ipc_send(comp_pid, &m);
}
static int clip_get(char *out, int max)
{
    message_t m, r; memset(&m, 0, sizeof(m));
    m.type = MSG_CLIP_GET;
    qk_sys_ipc_send(comp_pid, &m);
    while (1) { qk_ipc_recv(&r); if (r.type == MSG_CLIP_DATA) break; }
    int len = (int)r.data1;
    if (len > max - 1) len = max - 1;
    if (len > 0) memcpy(out, r.payload, len);
    out[len] = 0;
    return len;
}

static void sel_cols_for_line(int line, int len, int *c0, int *c1)
{
    *c0 = -1; *c1 = -1;
    if (sel_line0 < 0 || (sel_line0 == sel_line1 && sel_col0 == sel_col1)) return;
    int a_line, a_col, b_line, b_col;
    if (sel_line0 < sel_line1 || (sel_line0 == sel_line1 && sel_col0 <= sel_col1)) {
        a_line = sel_line0; a_col = sel_col0; b_line = sel_line1; b_col = sel_col1;
    } else {
        a_line = sel_line1; a_col = sel_col1; b_line = sel_line0; b_col = sel_col0;
    }
    if (line < a_line || line > b_line) return;
    if (line == a_line && line == b_line) { *c0 = a_col; *c1 = b_col; }
    else if (line == a_line) { *c0 = a_col; *c1 = len; }
    else if (line == b_line) { *c0 = 0; *c1 = b_col; }
    else { *c0 = 0; *c1 = len; }
}

static void term_xy_to_lc(int mx, int my, int *out_line, int *out_col)
{
    int step = 20, panel_h = 30;
    int input_y = gfx_h - panel_h;
    *out_line = -1; *out_col = 0;

    int row_from_bot = (input_y - my - 15) / step;
    if (row_from_bot < 0 || row_from_bot >= vis_map_n) return;
    int line = vis_map[row_from_bot];
    *out_line = line;
    int idx = lbuf_idx(line);
    int len = strlen(lbuf[idx]);
    int px = PAD_X;
    for (int c = 0; c < len; c++) {
        int cw = get_char_width(lbuf[idx][c]);
        if (mx < px + cw / 2) { *out_col = c; return; }
        px += cw;
    }
    *out_col = len;
}

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

static void term_render(void)
{
    if (!gfx) return;

    fill_rect(0, 0, gfx_w, gfx_h, COL_BG);

    int step    = 20;
    int panel_h = 30;
    int input_y = gfx_h - panel_h;
    int max_out = (input_y - PAD_Y) / step;
    if (max_out < 1) max_out = 1;
    output_rows = max_out;
    vis_rows    = max_out + 1;

    fill_rect(0, input_y, gfx_w, panel_h, 0xFF252525u);
    {
        int ty = input_y + (panel_h - cell_h) / 2 + 14;
        int x = draw_colored_line(PAD_X, ty, lcur, lcur_col, lcur_len);
        int cw = get_char_width('M'); if (cw < 2) cw = 8;
        fill_rect(x, ty - 14, cw, cell_h, COL_CURSOR);
    }

    vis_map_n = 0;
    int row = 0;
    for (int i = ltotal - 1 - scroll_up; i >= 0 && row < max_out; i--) {
        if (ltotal - i > MAX_LINES) break;
        int idx = lbuf_idx(i);
        int len = strlen(lbuf[idx]);
        if (len == 0) continue;
        int y = input_y - (row + 1) * step;
        if (y < PAD_Y) break;
        if (row < 128) vis_map[row] = i;
        int c0, c1;
        sel_cols_for_line(i, len, &c0, &c1);
        if (c0 >= 0) {
            gfx_context_t ctx = {gfx,(uint32_t)gfx_w,(uint32_t)gfx_h,(uint32_t)gfx_w};
            int x = PAD_X;
            for (int ci = 0; ci < len && lbuf[idx][ci]; ci++) {
                int cw = get_char_width(lbuf[idx][ci]);
                if (ci >= c0 && ci < c1)
                    fill_rect(x, y - 14, cw, cell_h, COL_SEL_BG);
                uint32_t color = ansi_pal[lcol[idx][ci] & 0xF];
                x = draw_codepoint_vector(&ctx, x, y, (uint8_t)lbuf[idx][ci], color);
            }
        } else {
            draw_colored_line(PAD_X, y, lbuf[idx], lcol[idx], len);
        }
        row++;
    }
    vis_map_n = row;

    qk_win_dirty(comp_pid);
}

static int spawn_ttysh(void)
{

    {
        int existing = -1;
        for (int _try = 0; _try < 200 && existing < 0; _try++) {
            existing = qk_get_server_pid(QK_SERVER_TTYSH);
            if (existing < 0) qk_sleep(10);
        }
        if (existing >= 0) {
            message_t attach; memset(&attach,0,sizeof(attach));
            attach.type  = MSG_TERM_ATTACH;
            attach.data1 = (uint32_t)qk_getpid();
            qk_sys_ipc_send(existing, &attach);
            return existing;
        }
    }

    int storage = qk_get_server_pid(QK_SERVER_STORAGE);
    if (storage < 0) return -1;

    message_t req; memset(&req,0,sizeof(req));
    req.type=MSG_DIR_REQ; req.data1=0;
    qk_sys_ipc_send(storage,&req);
    uint32_t apps_cluster=0;
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
            char name[64]; int ni=0;
            while (*ptr&&*ptr!='\n'&&ni<63) name[ni++]=*ptr++;
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

static void term_paste_to_ttysh(int tpid, const char *text, int len)
{

    static const uint8_t asc[26] = {
        0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,
        0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1F,0x14,
        0x16,0x2F,0x11,0x2D,0x15,0x2C
    };

    static const uint8_t dsc[10] = {
        0x0B,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A
    };
    for (int i = 0; i < len; i++) {
        char c = text[i];
        uint8_t sc = 0;
        uint32_t mods = 0;
        if      (c >= 'a' && c <= 'z') { sc = asc[c - 'a']; }
        else if (c >= 'A' && c <= 'Z') { sc = asc[c - 'A']; mods = 0x01; }
        else if (c >= '0' && c <= '9') { sc = dsc[c - '0']; }
        else if (c == ' ')  sc = 0x39;
        else if (c == '\n') sc = 0x1C;
        else if (c == '-')  sc = 0x0C;
        else if (c == '=')  sc = 0x0D;
        else if (c == '[')  sc = 0x1A;
        else if (c == ']')  sc = 0x1B;
        else if (c == ';')  sc = 0x27;
        else if (c == '\'') sc = 0x28;
        else if (c == '`')  sc = 0x29;
        else if (c == '\\') sc = 0x2B;
        else if (c == ',')  sc = 0x33;
        else if (c == '.')  sc = 0x34;
        else if (c == '/')  sc = 0x35;
        else if (c == '\t') sc = 0x0F;
        else if (c == '_')  { sc = 0x0C; mods = 0x01; }
        else if (c == '+')  { sc = 0x0D; mods = 0x01; }
        else if (c == '{')  { sc = 0x1A; mods = 0x01; }
        else if (c == '}')  { sc = 0x1B; mods = 0x01; }
        else if (c == ':')  { sc = 0x27; mods = 0x01; }
        else if (c == '"')  { sc = 0x28; mods = 0x01; }
        else if (c == '~')  { sc = 0x29; mods = 0x01; }
        else if (c == '|')  { sc = 0x2B; mods = 0x01; }
        else if (c == '<')  { sc = 0x33; mods = 0x01; }
        else if (c == '>')  { sc = 0x34; mods = 0x01; }
        else if (c == '?')  { sc = 0x35; mods = 0x01; }
        else if (c == '!')  { sc = 0x02; mods = 0x01; }
        else if (c == '@')  { sc = 0x03; mods = 0x01; }
        else if (c == '#')  { sc = 0x04; mods = 0x01; }
        else if (c == '$')  { sc = 0x05; mods = 0x01; }
        else if (c == '%')  { sc = 0x06; mods = 0x01; }
        else if (c == '^')  { sc = 0x07; mods = 0x01; }
        else if (c == '&')  { sc = 0x08; mods = 0x01; }
        else if (c == '*')  { sc = 0x09; mods = 0x01; }
        else if (c == '(')  { sc = 0x0A; mods = 0x01; }
        else if (c == ')')  { sc = 0x0B; mods = 0x01; }
        else continue;
        message_t km;
        memset(&km, 0, sizeof(km));
        km.type  = MSG_KEY_EVENT;
        km.data1 = (uint32_t)sc;
        km.data2 = mods;
        qk_sys_ipc_send(tpid, &km);
    }
}

int main(void)
{
    comp_pid = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    if (comp_pid < 0) return 1;

    void *font_addr = 0;
    if (qk_map_font_atlas(&font_addr) == 0)
        font_atlas_init((uint8_t *)font_addr);

    cell_h = get_font_height();
    if (cell_h < 8 || cell_h > 30) cell_h = 18;

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
            sel_line0 = -1;
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

        if (msg.type == MSG_MOUSE_EVENT) {
            Event *ev = (Event *)msg.payload;
            int mx = ev->x, my = ev->y - TITLE_BAR_HEIGHT;
            if (ev->type == EVENT_MOUSE_SCROLL) {
                scroll_up += ev->scroll;
                if (scroll_up < 0) scroll_up = 0;
                int max_s = ltotal + (lcur_len > 0 ? 1 : 0);
                if (scroll_up > max_s) scroll_up = max_s;
                sel_line0 = -1;
                term_render();
            } else if (ev->type == EVENT_MOUSE_DOWN && ev->button == MOUSE_BTN_LEFT) {
                int line, col;
                term_xy_to_lc(mx, my, &line, &col);
                if (line >= 0) {
                    sel_line0 = sel_line1 = line;
                    sel_col0 = sel_col1 = col;
                    term_sel = true;
                    term_render();
                }
            } else if (ev->type == EVENT_MOUSE_MOVE && term_sel) {
                int line, col;
                term_xy_to_lc(mx, my, &line, &col);
                if (line >= 0) {
                    sel_line1 = line;
                    sel_col1 = col;
                    term_render();
                }
            } else if (ev->type == EVENT_MOUSE_UP) {
                term_sel = false;
            }
            continue;
        }

        if (msg.type != MSG_KEY_EVENT) continue;

        uint32_t raw    = msg.data1;
        bool     is_rel = (raw & 0x80) != 0;
        uint8_t  code   = (uint8_t)(raw & 0x7F);
        bool     ext    = (raw & 0x100) != 0;

        static int awaiting_ext = 0;
        static bool ctrl_held = false;
        if (code == 0x1D) { ctrl_held = !is_rel; }
        if (!ext && code == 0x00) { awaiting_ext = 1; continue; }
        if (is_rel) { awaiting_ext = 0; continue; }

        if (awaiting_ext || ext) {
            awaiting_ext = 0;
            if (code == 0x49) {
                scroll_up += vis_rows / 2;
                int max = ltotal + (lcur_len>0?1:0);
                if (scroll_up > max) scroll_up = max;
                term_render();
            } else if (code == 0x51) {
                scroll_up -= vis_rows / 2;
                if (scroll_up < 0) scroll_up = 0;
                term_render();
            } else if (ttysh_pid > 0) {
                qk_sys_ipc_send(ttysh_pid, &msg);
            }
            continue;
        }

        if (code == 0x49) {
            scroll_up += vis_rows / 2;
            int max = ltotal + (lcur_len>0?1:0);
            if (scroll_up > max) scroll_up = max;
            term_render(); continue;
        }
        if (code == 0x51) {
            scroll_up -= vis_rows / 2;
            if (scroll_up < 0) scroll_up = 0;
            term_render(); continue;
        }

        if (ctrl_held && code == 0x2E &&
            sel_line0 >= 0 && !(sel_line0 == sel_line1 && sel_col0 == sel_col1)) {
            int a_line, a_col, b_line, b_col;
            if (sel_line0 < sel_line1 || (sel_line0 == sel_line1 && sel_col0 <= sel_col1)) {
                a_line = sel_line0; a_col = sel_col0; b_line = sel_line1; b_col = sel_col1;
            } else {
                a_line = sel_line1; a_col = sel_col1; b_line = sel_line0; b_col = sel_col0;
            }
            char cbuf[4096]; int clen = 0;
            for (int li = a_line; li <= b_line && clen < 4094; li++) {
                int idx = lbuf_idx(li);
                int len = strlen(lbuf[idx]);
                int c0 = (li == a_line) ? a_col : 0;
                int c1 = (li == b_line) ? b_col : len;
                if (c1 > len) c1 = len;
                for (int ci = c0; ci < c1 && clen < 4094; ci++)
                    cbuf[clen++] = lbuf[idx][ci];
                if (li < b_line && clen < 4094)
                    cbuf[clen++] = '\n';
            }
            cbuf[clen] = 0;
            clip_set(cbuf, clen);
            sel_line0 = -1;
            term_render();
            continue;
        }

        if (ctrl_held && code == 0x2F && ttysh_pid > 0) {
            char cbuf[4096];
            int clen = clip_get(cbuf, sizeof(cbuf));
            if (clen > 0)
                term_paste_to_ttysh(ttysh_pid, cbuf, clen);
            continue;
        }

        if (ttysh_pid > 0) qk_sys_ipc_send(ttysh_pid, &msg);
    }

    return 0;
}
