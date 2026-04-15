#include "gfx/events.h"
#include "kernel/ipc.h"
#include "lib/stdbool.h"
#include "lib/stdlib.h"
#include "user/lib/font_atlas.h"
#include "user/lib/gfx.h"
#include "user/lib/libapi.h"
#include "user/lib/storage_client.h"
#include "user/lib/window.h"
#define WIN_W  840
#define WIN_H  620
#define TOOLBAR_H 40
#define GUTTER_W  56
#define STATUS_H  22
#define TEXT_PAD   8   
#define BUF_SIZE 65536
#define COL_BG          0xFF1E2127u
#define COL_GUTTER_BG   0xFF181B21u
#define COL_ACTIVE_LINE 0xFF21252Bu
#define COL_TOOLBAR_BG  0xFF181B21u
#define COL_STATUS_BG   0xFF13151Au
#define COL_SEP         0xFF2C313Au
#define COL_ACCENT      0xFF61AFEFu
#define COL_TEXT        0xFFABB2BFu
#define COL_DIM         0xFF4B5263u
#define COL_COMMENT     0xFF5C6370u
#define COL_STRING      0xFF98C379u
#define COL_NUMBER      0xFFD19A66u
#define COL_KEYWORD     0xFFC678DDu
#define COL_BTN_SAVE_BG 0xFF253325u
#define COL_BTN_NEW_BG  0xFF1C2B3Au
#define COL_BTN_SAVE    0xFF98C379u
#define COL_BTN_NEW     0xFF61AFEFu
#define COL_CURSOR      0xFF528BFFu
#define COL_MOD_IND     0xFFE06C75u
static int comp_pid    = -1;
static int storage_pid = -1;
static char   tbuf[BUF_SIZE];
static int    tcursor   = 0;
static int    scroll_ln = 0;
static char   tfile[64] = "untitled.txt";
static bool   tmod      = false;
static uint32_t *gfx    = 0;
static int       gfx_w  = 0;
static int       gfx_h  = 0;
static int       win_id = -1;
static void fill(int x, int y, int w, int h, uint32_t c)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > gfx_w) w = gfx_w - x;
    if (y + h > gfx_h) h = gfx_h - y;
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++) {
        uint32_t *p = gfx + (y + row) * gfx_w + x;
        for (int k = 0; k < w; k++) p[k] = c;
    }
}
static void fill_rd(int x, int y, int w, int h, int r, uint32_t c)
{
    gfx_context_t ctx = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
    gfx_rect_rounded_fill(&ctx, x, y, w, h, r, c);
}
static void dstr(int x, int y, const char *s, uint32_t c)
{
    gfx_context_t ctx = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
    draw_string_vector(&ctx, x, y, s, c);
}
static int dch(int x, int y, char ch, uint32_t c)
{
    gfx_context_t ctx = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
    return draw_char_vector(&ctx, x, y, ch, c);
}
static int slen(const char *s) { int n=0; while(s[n]) n++; return n; }
static void scat(char *d, const char *s)
{
    int dl = slen(d), i = 0;
    while (s[i] && dl+i < 126) { d[dl+i] = s[i]; i++; }
    d[dl+i] = 0;
}
static bool is_alpha(char c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
static bool is_digit(char c) { return c>='0'&&c<='9'; }
static bool is_alnum(char c) { return is_alpha(c)||is_digit(c); }
static const char *kw[] = {
    "if","else","while","for","do","return","break","continue","switch","case",
    "int","char","void","bool","short","long","unsigned","signed","float","double",
    "uint8_t","uint16_t","uint32_t","int8_t","int16_t","int32_t",
    "static","const","extern","inline","struct","typedef","enum","union",
    "true","false","NULL","null",
    0
};
static bool is_keyword(const char *p, int len)
{
    for (int i = 0; kw[i]; i++) {
        int kl = slen(kw[i]);
        if (kl != len) continue;
        bool ok = true;
        for (int j = 0; j < kl; j++) if (kw[i][j] != p[j]) { ok=false; break; }
        if (ok) return true;
    }
    return false;
}
static void cursor_lc(int *lo, int *co)
{
    int l=0, c=0;
    for (int i=0; i<tcursor; i++) {
        if (tbuf[i]=='\n') { l++; c=0; } else c++;
    }
    *lo=l; *co=c;
}
static int total_lines(void)
{
    int n=1;
    for (char *p=tbuf; *p; p++) if(*p=='\n') n++;
    return n;
}
static void render(void)
{
    if (!gfx || !user_font) return;
    int fh = get_font_height();
    if (fh <= 0) return;
    int safe_bot  = gfx_h - WIN_CONTENT_GAP;
    int text_top  = TOOLBAR_H;
    int text_bot  = safe_bot - STATUS_H;
    int text_h    = text_bot - text_top;
    int vis_lines = text_h / fh;
    if (vis_lines < 1) vis_lines = 1;
    int cline, ccol;
    cursor_lc(&cline, &ccol);
    if (cline < scroll_ln) scroll_ln = cline;
    if (cline >= scroll_ln + vis_lines) scroll_ln = cline - vis_lines + 1;
    if (scroll_ln < 0) scroll_ln = 0;
    fill(0, 0, gfx_w, gfx_h, COL_BG);
    fill(0, 0, gfx_w, TOOLBAR_H, COL_TOOLBAR_BG);
    fill(0, TOOLBAR_H-1, gfx_w, 1, COL_SEP);
    int tb_text_y = (TOOLBAR_H - fh) / 2;
    fill_rd(8, 7, 62, 26, 5, COL_BTN_SAVE_BG);
    {
        int sw = 0;
        for (const char *p = "Save"; *p; p++) sw += get_char_width(*p);
        dstr(8 + (62 - sw) / 2, tb_text_y, "Save", COL_BTN_SAVE);
    }
    fill_rd(76, 7, 50, 26, 5, COL_BTN_NEW_BG);
    {
        int nw = 0;
        for (const char *p = "New"; *p; p++) nw += get_char_width(*p);
        dstr(76 + (50 - nw) / 2, tb_text_y, "New", COL_BTN_NEW);
    }
    fill(132, 10, 1, 20, COL_SEP);
    {
        char title[80];
        if (tmod) {
            strncpy(title, "* ", 79);
            int tl = slen(title);
            strncpy(title+tl, tfile, 79-tl);
        } else {
            strncpy(title, tfile, 79);
        }
        title[79] = 0;
        int tw = 0;
        for (int i=0; title[i]; i++) tw += get_char_width(title[i]);
        int tx = gfx_w/2 - tw/2;
        if (tx < 140) tx = 140;
        dstr(tx, tb_text_y, title, tmod ? COL_MOD_IND : COL_DIM);
    }
    fill(0, text_top, GUTTER_W, text_h, COL_GUTTER_BG);
    fill(GUTTER_W, text_top, 1, text_h, COL_SEP);
    {
        int aly = text_top + (cline - scroll_ln) * fh;
        if (aly >= text_top && aly < text_bot)
            fill(GUTTER_W+1, aly, gfx_w-GUTTER_W-1, fh, COL_ACTIVE_LINE);
    }
    char *ptr = tbuf;
    int lnum = 0;
    while (*ptr && lnum < scroll_ln) {
        if (*ptr == '\n') lnum++;
        ptr++;
    }
    for (int vis = 0; vis < vis_lines; vis++) {
        if (!*ptr && lnum > 0) break;
        int ly = text_top + vis * fh;
        {
            char lnbuf[8];
            int_to_str(lnum+1, lnbuf, 0);
            int lx = GUTTER_W - 8;
            for (int ci = slen(lnbuf)-1; ci >= 0; ci--)
                lx -= get_char_width(lnbuf[ci]);
            dstr(lx, ly, lnbuf, (lnum == cline) ? COL_ACCENT : COL_DIM);
        }
        int px = GUTTER_W + TEXT_PAD + 1;
        typedef enum { S_NORMAL, S_COMMENT, S_STR_DBL, S_STR_SNG } Syn;
        Syn syn = S_NORMAL;
        const char *tok_end = ptr;
        uint32_t    tok_col = COL_TEXT;
        char *line_start = ptr;
        while (*ptr && *ptr != '\n') {
            if (ptr == tbuf + tcursor)
                fill(px, ly+1, 2, fh-2, COL_CURSOR);
            char c = *ptr;
            uint32_t col;
            if (syn == S_COMMENT) {
                col = COL_COMMENT;
            } else if (syn == S_STR_DBL) {
                col = COL_STRING;
                if (c == '"' && (ptr == line_start || *(ptr-1) != '\\'))
                    syn = S_NORMAL;
            } else if (syn == S_STR_SNG) {
                col = COL_STRING;
                if (c == '\'' && (ptr == line_start || *(ptr-1) != '\\'))
                    syn = S_NORMAL;
            } else {
                if (ptr >= tok_end) {
                    if (c == '#') {
                        syn = S_COMMENT; tok_col = COL_COMMENT; tok_end = ptr+1;
                    } else if (c == '/' && *(ptr+1) == '/') {
                        syn = S_COMMENT; tok_col = COL_COMMENT; tok_end = ptr+1;
                    } else if (c == '"') {
                        syn = S_STR_DBL; tok_col = COL_STRING; tok_end = ptr+1;
                    } else if (c == '\'') {
                        syn = S_STR_SNG; tok_col = COL_STRING; tok_end = ptr+1;
                    } else if (is_digit(c) &&
                               (ptr == line_start || !is_alnum(*(ptr-1)))) {
                        const char *np = ptr;
                        while (is_digit(*np) || *np == '.' ||
                               *np == 'x' || *np == 'X' ||
                               (*np >= 'a' && *np <= 'f') ||
                               (*np >= 'A' && *np <= 'F'))
                            np++;
                        tok_end = np;
                        tok_col = COL_NUMBER;
                    } else if (is_alpha(c)) {
                        const char *wp = ptr;
                        while (is_alnum(*wp)) wp++;
                        tok_end = wp;
                        tok_col = is_keyword(ptr, (int)(wp - ptr))
                                  ? COL_KEYWORD : COL_TEXT;
                    } else {
                        tok_end = ptr+1;
                        tok_col = COL_TEXT;
                    }
                }
                col = tok_col;
            }
            if (c == '\t') {
                int tw = get_char_width(' ') * 4;
                px += tw;
                ptr++;
            } else {
                px = dch(px, ly, c, col);
                ptr++;
            }
        }
        if (ptr == tbuf + tcursor)
            fill(px, ly+1, 2, fh-2, COL_CURSOR);
        if (*ptr == '\n') { lnum++; ptr++; }
        else break;
    }
    if (ptr == tbuf + tcursor && !*ptr) {
        int aly = text_top + (cline - scroll_ln) * fh;
        if (aly >= text_top && aly < text_bot)
            fill(GUTTER_W + TEXT_PAD + 1, aly+1, 2, fh-2, COL_CURSOR);
    }
    int sy = safe_bot - STATUS_H;
    fill(0, sy, gfx_w, STATUS_H, COL_STATUS_BG);
    fill(0, sy, gfx_w, 1, COL_SEP);
    {
        char sb[128] = "  ";
        char tmp[16];
        scat(sb, "Ln ");
        int_to_str(cline+1, tmp, 0); scat(sb, tmp);
        scat(sb, "  Col ");
        int_to_str(ccol+1, tmp, 0); scat(sb, tmp);
        scat(sb, "  \xB7  ");
        int_to_str(total_lines(), tmp, 0); scat(sb, tmp);
        scat(sb, " lines");
        int sb_text_y = sy + (STATUS_H - fh) / 2;
        dstr(8, sb_text_y, sb, COL_DIM);
        const char *hint = "Ctrl+S: Save  ";
        int hx = gfx_w;
        for (int i = slen(hint)-1; i >= 0; i--)
            hx -= get_char_width(hint[i]);
        dstr(hx, sb_text_y, hint, COL_DIM);
    }
    qk_win_dirty(comp_pid);
}
static char scancode_to_char(uint8_t code, bool shift)
{
    if (code == 0x39) return ' ';
    if (code == 0x1C) return '\n';
    if (code == 0x0E) return '\b';
    if (code == 0x0F) return '\t';
    const char *r1  = "1234567890-=";
    const char *r1s = "!@#$%^&*()_+";
    if (code >= 0x02 && code <= 0x0D) return shift ? r1s[code-0x02] : r1[code-0x02];
    const char *r2  = "qwertyuiop[]";
    const char *r2s = "QWERTYUIOP{}";
    if (code >= 0x10 && code <= 0x1B) return shift ? r2s[code-0x10] : r2[code-0x10];
    if (code == 0x2B) return shift ? '|' : '\\';
    const char *r3  = "asdfghjkl;'`";
    const char *r3s = "ASDFGHJKL:\"~";
    if (code >= 0x1E && code <= 0x29) return shift ? r3s[code-0x1E] : r3[code-0x1E];
    const char *r4  = "zxcvbnm,./";
    const char *r4s = "ZXCVBNM<>?";
    if (code >= 0x2C && code <= 0x35) return shift ? r4s[code-0x2C] : r4[code-0x2C];
    return 0;
}
static void save_file(void)
{
    uint32_t len = (uint32_t)strlen(tbuf);
    storage_send_file(storage_pid, 0, tfile, (const uint8_t *)tbuf, len);
    tmod = false;
}
int main(void)
{
    comp_pid    = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    storage_pid = qk_get_server_pid(QK_SERVER_STORAGE);
    if (comp_pid < 0 || storage_pid < 0) return 1;
    void *font_addr = 0;
    if (qk_map_font_atlas(&font_addr) == 0)
        font_atlas_init((uint8_t *)font_addr);
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type  = MSG_WIN_CREATE;
    msg.data1 = WIN_W;
    msg.data2 = WIN_H;
    strncpy((char *)msg.payload, "Notepad", 63);
    qk_sys_ipc_send(comp_pid, &msg);
    while (1) {
        qk_ipc_recv(&msg);
        if (msg.type == MSG_WIN_CREATED) break;
        if (msg.type == MSG_FILE_OPEN && msg.payload[0] != '\0')
            strncpy(tfile, (char *)msg.payload, 63);
    }
    win_id = (int)msg.data1;
    gfx = (uint32_t *)qk_win_map_buffer(comp_pid, win_id, &gfx_w, &gfx_h);
    {
        message_t probe;
        while (qk_ipc_recv_nb(&probe) == 0) {
            if (probe.type == MSG_FILE_OPEN && probe.payload[0] != '\0') {
                strncpy(tfile, (char *)probe.payload, 63);
                tfile[63] = 0;
                break;
            }
        }
    }
    memset(tbuf, 0, BUF_SIZE);
    if (tfile[0] == '/') {
        message_t req, resp;
        memset(&req, 0, sizeof(req));
        req.type  = MSG_FILE_OPEN;
        req.data1 = 0xFFFF;
        strncpy((char *)req.payload, tfile, sizeof(req.payload) - 1);
        qk_sys_ipc_send(storage_pid, &req);
        while (1) { qk_ipc_recv(&resp); if (resp.sender_pid == storage_pid) break; }
        if (resp.type == MSG_FILE_SIZE && resp.data1 > 0) {
            uint32_t size = resp.data1;
            if (size > (uint32_t)(BUF_SIZE - 1)) size = (uint32_t)(BUF_SIZE - 1);
            uint32_t off = 0;
            while (off < size) {
                memset(&req, 0, sizeof(req));
                req.type  = MSG_REQ_DATA;
                req.data1 = off;
                req.data2 = (uint32_t)sizeof(resp.payload);
                qk_sys_ipc_send(storage_pid, &req);
                while (1) { qk_ipc_recv(&resp); if (resp.sender_pid == storage_pid && resp.type == MSG_FILE_DATA) break; }
                uint32_t got = resp.data1;
                if (got == 0) break;
                if (off + got > size) got = size - off;
                memcpy(tbuf + off, resp.payload, got);
                off += got;
            }
            tbuf[off] = 0;
            tcursor = (int)off;
        }
    } else {
        const char *init = "# Welcome to Notepad\n# Ctrl+S: Save   New button: clear buffer\n\n";
        strncpy(tbuf, init, BUF_SIZE - 1);
        tcursor = slen(tbuf);
        strncpy(tfile, "untitled.txt", 63);
    }
    render();
    bool lshift=false, rshift=false, lctrl=false;
    while (1) {
        qk_ipc_recv(&msg);
        if (msg.type == MSG_WIN_RESIZED) {
            uint32_t *ng = (uint32_t *)qk_win_map_buffer(comp_pid, win_id, &gfx_w, &gfx_h);
            if (ng) gfx = ng;
            render();
            continue;
        }
        if (msg.type == MSG_KEY_EVENT) {
            uint8_t sc = (uint8_t)msg.data1;
            bool released = (sc & 0x80) != 0;
            sc &= 0x7F;
            if (sc == 0x2A) { lshift = !released; continue; }
            if (sc == 0x36) { rshift = !released; continue; }
            if (sc == 0x1D) { lctrl  = !released; continue; }
            if (released) continue;
            if (lctrl && sc == 0x1F) { save_file(); render(); continue; }
            if (sc == 0x49) {
                int fh = get_font_height();
                int v  = (fh > 0) ? (gfx_h - TOOLBAR_H - STATUS_H) / fh : 10;
                scroll_ln -= v;
                if (scroll_ln < 0) scroll_ln = 0;
                render(); continue;
            }
            if (sc == 0x51) {
                int fh = get_font_height();
                int v  = (fh > 0) ? (gfx_h - TOOLBAR_H - STATUS_H) / fh : 10;
                int tot = total_lines();
                scroll_ln += v;
                if (scroll_ln >= tot) scroll_ln = tot-1;
                render(); continue;
            }
            bool shift_active = lshift || rshift || ((msg.data2 & 0x08) != 0) ;
            char c = scancode_to_char(sc, shift_active);
            if (c) {
                if (c == '\b') {
                    if (tcursor > 0) { tcursor--; tbuf[tcursor] = 0; }
                } else if (tcursor < BUF_SIZE - 1) {
                    tbuf[tcursor++] = c;
                    tbuf[tcursor]   = 0;
                }
                tmod = true;
                render();
            }
            continue;
        }
        if (msg.type == MSG_MOUSE_EVENT) {
            Event *e = (Event *)msg.payload;
            if (e->type == EVENT_MOUSE_SCROLL) {
                int tot = total_lines();
                scroll_ln += e->scroll;
                if (scroll_ln < 0) scroll_ln = 0;
                if (scroll_ln >= tot) scroll_ln = tot-1;
                render();
                continue;
            }
            if (e->type == EVENT_MOUSE_DOWN && e->button == MOUSE_BTN_LEFT) {
                int x  = e->x;
                int cy = e->y - TITLE_BAR_HEIGHT; 
                if (cy >= 7 && cy < 33) {
                    if (x >= 8 && x < 70) {
                        save_file(); render();
                    }
                    else if (x >= 76 && x < 126) {
                        memset(tbuf, 0, BUF_SIZE);
                        const char *blank = "# New file\n\n";
                        strncpy(tbuf, blank, BUF_SIZE-1);
                        tcursor = strlen(tbuf);
                        scroll_ln = 0;
                        strncpy(tfile, "untitled.txt", 63);
                        tmod = false;
                        render();
                    }
                }
            }
        }
    }
    return 0;
}
