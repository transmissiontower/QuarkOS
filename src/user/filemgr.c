#include "kernel/ipc.h"
#include "lib/stdbool.h"
#include "lib/stdlib.h"
#include "user/lib/font_atlas.h"
#include "user/lib/gfx.h"
#include "user/lib/icons_def.h"
#include "user/lib/libapi.h"
#include "user/lib/window.h"
#define WIN_W  900
#define WIN_H  580
#define HEADER_H   34
#define SIDEBAR_W  185
#define CONTENT_X  SIDEBAR_W
#define CONTENT_Y  HEADER_H
#define SB_ITEM_H  28
#define SB_ICON_SZ 16
#define SCROLLBAR_W 8
#define GCOLS      4
#define GITEM_W    160
#define GITEM_H    110
#define GICON_W    100
#define GICON_H    80
#define CB_HEADER   0xFF192425u
#define CB_SIDEBAR  0xFF1B2526u
#define CB_CONTENT  0xFF1E2830u
#define CB_SEL      0xFF2C4446u
#define CB_HOVER    0xFF283C3Eu
#define C_SEP       0xFF293939u
#define C_TEXT      0xFFDDDDDDu
#define C_DIM       0xFF6B7C7Cu
#define C_ACCENT    0xFF59B2D4u
#define CF_BODY     0xFF78AEE8u  
#define CF_TOP      0xFF5A8FD0u  
#define CF_TAB      0xFF1F5DA8u  
#define CF_SHADE    0xFF96C3F0u  
#define CF_SHADOW   0xFF2A4E8Au  
#define CF_FILE     0xFFDCE3EAu
#define CF_FFOLD    0xFFAAB6C2u
#define CF_EXE      0xFF80CC80u
#define CF_EFOLD    0xFF52A452u
#define C_SBICO     0xFF8BAAB2u
static int       comp_pid    = -1;
static int       storage_pid = -1;
static uint32_t *gfx         = 0;
static int       gfx_w       = 0;
static int       gfx_h       = 0;
static int       win_id      = -1;
#define MAX_DEPTH   12
static uint16_t dir_stack[MAX_DEPTH];
static char     dir_names[MAX_DEPTH][16];
static int      dir_depth   = 0;
static uint16_t cur_cluster = 0;
static char     cur_path[128] = "/";
#define MAX_ENTRIES 128
typedef struct {
    int      is_dir;
    uint16_t cluster;
    char     name[16];
} fm_entry_t;
static fm_entry_t entries[MAX_ENTRIES];
static int        entry_count = 0;
static int        selected    = -1;
static int        grid_scroll = 0;   
static int        hover_sb    = -1;  
static int        hover_grid  = -1;  
typedef struct {
    const char *label;
    const char *path;
    int         kind;    
    uint32_t    icon_id; 
} sb_item_t;
static const sb_item_t sb[] = {
    {"Home",      "/USERS/DEFAULT",      0, ICON_HOME},
    {"Apps",      "/APPS",               0, ICON_TERMINAL},
    {"System",    "/SYSTEM",             0, ICON_MONITOR},
    {"Users",     "/USERS",              0, ICON_USERS},
    {"Temp",      "/TEMP",               0, ICON_CLOCK},
    {0,           0,                     1, 0},
    {"Documents", "/USERS/DEFAULT/DOCS", 0, ICON_FILE_TEXT},
    {"Desktop",   "/USERS/DEFAULT/DESK", 0, ICON_FOLDER_OPEN},
    {0,           0,                     1, 0},
    {"Root",      "/",                   0, ICON_HARD_DRIVE},
};
#define SB_N  10
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
static void fill_round(int x, int y, int w, int h, uint32_t c)
{
    if (w <= 0 || h <= 0) return;
    fill(x + 2, y,     w - 4, h,     c);
    fill(x,     y + 2, 2,     h - 4, c);
    fill(x+w-2, y + 2, 2,     h - 4, c);
    fill(x + 1, y + 1, 1, 1, c);
    fill(x+w-2, y + 1, 1, 1, c);
    fill(x + 1, y+h-2, 1, 1, c);
    fill(x+w-2, y+h-2, 1, 1, c);
}
static void dstr(int x, int y, const char *s, uint32_t c)
{
    gfx_context_t ctx = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
    draw_string_vector(&ctx, x, y, s, c);
}
static int str_w(const char *s)
{
    int w = 0;
    while (*s) { w += get_char_width(*s); s++; }
    return w;
}
static bool str_ends_ext(const char *n, const char *ext3)
{
    int l = 0;
    while (n[l]) l++;
    if (l < 4 || n[l-4] != '.') return false;
    for (int i = 0; i < 3; i++) {
        char c = n[l-3+i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c != ext3[i]) return false;
    }
    return true;
}
static int str_eq_ci(const char *a, const char *b)
{
    for (;;) {
        char ca = *a++, cb = *b++;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
        if (!ca) return 1;
    }
}
static void draw_folder_icon(int x, int y, int w, int h)
{
    int tab_h  = h / 6;               
    int tab_w  = (w * 50) / 100;      
    int body_y = y + tab_h;
    int body_h = h - tab_h;
    fill(x + 4, y + h + 1, w - 4, 3, CF_SHADOW);
    fill(x + w, body_y + 2, 3, body_h - 2, CF_SHADOW);
    fill(x, body_y, w, body_h, CF_BODY);
    fill(x, body_y, w, 3, CF_SHADE);
    fill(x, body_y + body_h - 5, w, 5, CF_TOP);
    fill(x, y, tab_w, tab_h, CF_TAB);
    fill(x + tab_w, body_y, 2, 2, CF_BODY);
}
static void draw_file_icon(int x, int y, int w, int h, uint32_t bg, uint32_t fold)
{
    int fc = w / 5;
    fill(x,           y,       w - fc, h,       bg);
    fill(x + w - fc,  y + fc,  fc,     h - fc,  bg);
    fill(x + w - fc,  y,       fc,     fc,      CB_CONTENT);
    fill(x + w - fc,  y,       1,      fc,      fold);
    fill(x + w - fc,  y + fc - 1, fc,  1,       fold);
}
static void rebuild_path(void)
{
    if (dir_depth == 0) { cur_path[0]='/'; cur_path[1]=0; return; }
    int pos = 0; cur_path[0] = 0;
    for (int i = 1; i <= dir_depth; i++) {
        if (pos < 126) cur_path[pos++] = '/';
        for (int j = 0; dir_names[i][j] && pos < 126; j++)
            cur_path[pos++] = dir_names[i][j];
    }
    cur_path[pos] = 0;
}
static void load_dir(void)
{
    entry_count = 0; selected = -1; grid_scroll = 0;
    message_t req;
    memset(&req, 0, sizeof(req));
    req.type  = MSG_DIR_REQ;
    req.data1 = cur_cluster;
    qk_sys_ipc_send(storage_pid, &req);
    while (1) {
        message_t resp;
        qk_ipc_recv(&resp);
        if (resp.sender_pid != storage_pid || resp.type != MSG_DIR_REPLY) continue;
        char *ptr = (char *)resp.payload;
        while (*ptr && entry_count < MAX_ENTRIES) {
            fm_entry_t e;
            e.is_dir = (*ptr == '1') ? 1 : 0;
            while (*ptr && *ptr != '|') ptr++;
            if (*ptr == '|') ptr++;
            int cl = 0;
            while (*ptr >= '0' && *ptr <= '9') { cl = cl*10 + (*ptr-'0'); ptr++; }
            e.cluster = (uint16_t)cl;
            if (*ptr == '|') ptr++;
            int ni = 0;
            while (*ptr && *ptr != '\n' && ni < 15) e.name[ni++] = *ptr++;
            e.name[ni] = 0;
            if (*ptr == '\n') ptr++;
            if (ni > 0 && !(e.name[0] == '.' && (ni == 1 || (ni == 2 && e.name[1] == '.'))))
                entries[entry_count++] = e;
        }
        if (resp.data1 == 0) break;
    }
}
static int navigate_to_path(const char *path)
{
    if (!path || path[0] != '/') return 0;
    dir_depth   = 0;
    cur_cluster = 0;
    if (path[1] == 0) { rebuild_path(); return 1; }
    const char *p = path + 1;
    while (*p) {
        char seg[16];
        int si = 0;
        while (*p && *p != '/' && si < 15) seg[si++] = *p++;
        seg[si] = 0;
        if (*p == '/') p++;
        if (si == 0) continue;
        message_t req;
        memset(&req, 0, sizeof(req));
        req.type  = MSG_DIR_REQ;
        req.data1 = cur_cluster;
        qk_sys_ipc_send(storage_pid, &req);
        int found = 0;
        uint16_t fc = 0;
        while (1) {
            message_t resp;
            qk_ipc_recv(&resp);
            if (resp.sender_pid != storage_pid || resp.type != MSG_DIR_REPLY) continue;
            char *rp = (char *)resp.payload;
            while (*rp && !found) {
                int is_d = (*rp == '1');
                while (*rp && *rp != '|') rp++;
                if (*rp == '|') rp++;
                int cl = 0;
                while (*rp >= '0' && *rp <= '9') { cl = cl*10 + (*rp-'0'); rp++; }
                if (*rp == '|') rp++;
                char nm[16]; int ni = 0;
                while (*rp && *rp != '\n' && ni < 15) nm[ni++] = *rp++;
                nm[ni] = 0;
                if (*rp == '\n') rp++;
                if (is_d && ni > 0 && str_eq_ci(seg, nm)) {
                    found = 1; fc = (uint16_t)cl;
                }
            }
            if (resp.data1 == 0) break;
        }
        if (!found) return 0;
        if (dir_depth < MAX_DEPTH - 1) {
            dir_depth++;
            dir_stack[dir_depth] = fc;
            strncpy(dir_names[dir_depth], seg, 15);
            dir_names[dir_depth][15] = 0;
            cur_cluster = fc;
        }
    }
    rebuild_path();
    return 1;
}
static void render(void)
{
    if (!gfx) return;
    fill(0, 0, gfx_w, gfx_h, CB_CONTENT);
    if (!user_font) { qk_win_dirty(comp_pid); return; }
    int fh = get_font_height();
    if (fh <= 0) { qk_win_dirty(comp_pid); return; }
    int content_w = gfx_w - SIDEBAR_W;
    int content_h = gfx_h - HEADER_H;
    int item_w    = GITEM_W;
    fill(0, 0, gfx_w, HEADER_H, CB_HEADER);
    fill(0, HEADER_H - 1, gfx_w, 1, C_SEP);
    {
        int tw = str_w("Files");
        dstr((SIDEBAR_W - tw) / 2, (HEADER_H - fh) / 2, "Files", C_TEXT);
    }
    fill(SIDEBAR_W - 1, 0, 1, HEADER_H, C_SEP);
    {
        int icon_y = (HEADER_H - 16) / 2;
        gfx_context_t ctx2 = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
        if (gui_icons) {
            draw_icon_sized(&ctx2, gui_icons, CONTENT_X + 8,  icon_y,
                            ICON_ARROW_LEFT,  dir_depth > 0 ? C_TEXT : C_DIM, 16);
            draw_icon_sized(&ctx2, gui_icons, CONTENT_X + 30, icon_y,
                            ICON_ARROW_RIGHT, C_DIM, 16);
        } else {
            dstr(CONTENT_X + 10, (HEADER_H - fh) / 2, "<",
                 dir_depth > 0 ? C_TEXT : C_DIM);
            dstr(CONTENT_X + 26, (HEADER_H - fh) / 2, ">", C_DIM);
        }
    }
    {
        int pill_x = CONTENT_X + 54;
        int pill_y = 5;
        int pill_w = content_w - 62;
        int pill_h = HEADER_H - 10;
        fill_round(pill_x, pill_y, pill_w, pill_h, CB_SIDEBAR);
        const char *disp = (cur_path[0]=='/'&&cur_path[1]==0) ? "Root" : cur_path;
        dstr(pill_x + 10, pill_y + (pill_h - fh) / 2, disp, C_TEXT);
    }
    fill(0, HEADER_H, SIDEBAR_W, gfx_h - HEADER_H, CB_SIDEBAR);
    fill(SIDEBAR_W - 1, HEADER_H, 1, gfx_h - HEADER_H, C_SEP);
    int active_sb = -1;
    for (int i = 0; i < SB_N; i++) {
        if (sb[i].kind == 0 && sb[i].path && str_eq_ci(cur_path, sb[i].path)) {
            active_sb = i; break;
        }
    }
    {
        int sy = HEADER_H + 8;
        for (int i = 0; i < SB_N; i++) {
            if (sb[i].kind == 1) {
                fill(12, sy + 4, SIDEBAR_W - 24, 1, C_SEP);
                sy += 12;
                continue;
            }
            if (!sb[i].label) continue;
            if (i == active_sb)
                fill_round(6, sy + 2, SIDEBAR_W - 14, SB_ITEM_H - 4, CB_SEL);
            else if (i == hover_sb)
                fill_round(6, sy + 2, SIDEBAR_W - 14, SB_ITEM_H - 4, CB_HOVER);
            uint32_t col = i == active_sb ? C_TEXT : C_DIM;
            int row_y = sy + (SB_ITEM_H - fh) / 2;
            if (sb[i].icon_id && gui_icons) {
                gfx_context_t ctx2 = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
                int icon_y = sy + (SB_ITEM_H - SB_ICON_SZ) / 2;
                draw_icon_sized(&ctx2, gui_icons, 12, icon_y, sb[i].icon_id, col, SB_ICON_SZ);
            }
            dstr(38, row_y, sb[i].label, col);
            sy += SB_ITEM_H;
        }
    }
    if (entry_count == 0) {
        const char *msg = "Empty directory";
        int tw = str_w(msg);
        dstr(CONTENT_X + (content_w - tw) / 2,
             CONTENT_Y + (content_h - fh) / 2, msg, C_DIM);
        qk_win_dirty(comp_pid);
        return;
    }
    int rows_total = (entry_count + GCOLS - 1) / GCOLS;
    int rows_vis   = content_h / GITEM_H;
    if (rows_vis < 1) rows_vis = 1;
    int max_scroll = rows_total - rows_vis;
    if (max_scroll < 0) max_scroll = 0;
    if (grid_scroll > max_scroll) grid_scroll = max_scroll;
    if (grid_scroll < 0) grid_scroll = 0;
    int icon_draw = 48;
    for (int row = 0; row <= rows_vis; row++) {
        int real_row = row + grid_scroll;
        if (real_row >= rows_total) break;
        for (int col = 0; col < GCOLS; col++) {
            int idx = real_row * GCOLS + col;
            if (idx >= entry_count) break;
            fm_entry_t *e = &entries[idx];
            int cx = CONTENT_X + col * item_w;
            int cy = CONTENT_Y + row * GITEM_H;
            if (cy >= gfx_h) continue;
            int visible_h = gfx_h - cy;
            if (visible_h <= 0) continue;
            if (idx == selected || idx == hover_grid) {
                int bx = cx + 10, by = cy + 6;
                int bw = item_w - 20;
                int bh = GITEM_H - 12;
                if (bh > visible_h - 4) bh = visible_h - 4;
                fill_round(bx, by, bw, bh,
                           idx == selected ? CB_SEL : CB_HOVER);
            }
            if (gui_icons) {
                #define GICON_SZ 40
                int ix = cx + (item_w - GICON_SZ) / 2;
                int iy = cy + 20;
                gfx_context_t ctx2 = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
                uint32_t icon_id = e->is_dir ? ICON_FOLDER :
                                   str_ends_ext(e->name, "QEX") || str_ends_ext(e->name, "ELF") ? ICON_TERMINAL :
                                   str_ends_ext(e->name, "BIN") ? ICON_BINARY : ICON_FILE_TEXT;
                uint32_t ic = e->is_dir ? C_ACCENT :
                              str_ends_ext(e->name, "QEX") || str_ends_ext(e->name, "ELF") ? 0xFF80CC80u :
                              str_ends_ext(e->name, "BIN") ? 0xFFCC9944u : C_TEXT;
                draw_icon_sized(&ctx2, gui_icons, ix, iy, icon_id, ic, GICON_SZ);
                (void)icon_draw;
            } else {
                int ix = cx + (item_w - GICON_W) / 2;
                int iy = cy + 18;
                if (iy + GICON_H <= gfx_h) {
                    if (e->is_dir)
                        draw_folder_icon(ix, iy, GICON_W, GICON_H);
                    else if (str_ends_ext(e->name, "QEX"))
                        draw_file_icon(ix, iy, GICON_W, GICON_H, CF_EXE, CF_EFOLD);
                    else
                        draw_file_icon(ix, iy, GICON_W, GICON_H, CF_FILE, CF_FFOLD);
                }
            }
            int ty = cy + 20 + GICON_SZ + 6; 
            if (ty + fh <= gfx_h) {
                char label[16];
                strncpy(label, e->name, 15); label[15] = 0;
                int tw = str_w(label);
                while (tw > item_w - 8 && label[0]) {
                    int l = 0; while (label[l]) l++;
                    if (l <= 1) break;
                    label[l-1] = 0;
                    tw = str_w(label);
                }
                int tx = cx + (item_w - tw) / 2;
                dstr(tx, ty, label, C_TEXT);
            }
        }
    }
    if (rows_total > rows_vis) {
        int sx  = gfx_w - SCROLLBAR_W;
        int sy  = CONTENT_Y;
        int sh  = content_h;
        fill(sx, sy, SCROLLBAR_W, sh, CB_SIDEBAR);
        int th = sh * rows_vis / rows_total;
        if (th < 16) th = 16;
        int ty2 = sy;
        if (rows_total > rows_vis)
            ty2 = sy + (sh - th) * grid_scroll / (rows_total - rows_vis);
        fill_round(sx + 1, ty2, SCROLLBAR_W - 2, th, C_DIM);
    }
    qk_win_dirty(comp_pid);
}
static int spawn_qex(const char *qex_path)
{
    message_t req, resp;
    memset(&req, 0, sizeof(req));
    req.type  = MSG_FILE_OPEN;
    req.data1 = 0xFFFF;
    strncpy((char *)req.payload, qex_path, sizeof(req.payload) - 1);
    qk_ipc_send_recv(storage_pid, &req, &resp);
    if (resp.type != MSG_FILE_SIZE || resp.data1 == 0) return -1;
    uint32_t size = resp.data1;
    uint8_t *buf  = (uint8_t *)malloc(size);
    if (!buf) return -1;
    uint32_t off = 0;
    while (off < size) {
        memset(&req, 0, sizeof(req));
        req.type  = MSG_REQ_DATA;
        req.data1 = off;
        req.data2 = sizeof(resp.payload);
        qk_ipc_send_recv(storage_pid, &req, &resp);
        if (resp.type != MSG_FILE_DATA) break;
        uint32_t got = resp.data1;
        if (got == 0) break;
        if (off + got > size) got = size - off;
        memcpy(buf + off, resp.payload, got);
        off += got;
    }
    int pid = (off > 0) ? qk_spawn(buf, size) : -1;
    free(buf);
    return pid;
}
static void open_with(const char *app_qex, const char *file_path)
{
    int pid = spawn_qex(app_qex);
    if (pid < 0) return;
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_FILE_OPEN;
    strncpy((char *)msg.payload, file_path, sizeof(msg.payload) - 1);
    qk_sys_ipc_send(pid, &msg);
}
static void launch_entry(int idx)
{
    if (idx < 0 || idx >= entry_count) return;
    fm_entry_t *e = &entries[idx];
    if (e->is_dir) {
        if (dir_depth < MAX_DEPTH - 1) {
            dir_depth++;
            dir_stack[dir_depth] = e->cluster;
            strncpy(dir_names[dir_depth], e->name, 15);
            dir_names[dir_depth][15] = 0;
            cur_cluster = e->cluster;
        }
        rebuild_path();
        load_dir();
        render();
        return;
    }
    char full_path[128];
    strncpy(full_path, cur_path, sizeof(full_path) - 20);
    int plen = strlen(full_path);
    if (plen > 0 && full_path[plen-1] != '/') full_path[plen++] = '/';
    strncpy(full_path + plen, e->name, sizeof(full_path) - plen - 1);
    full_path[sizeof(full_path)-1] = 0;
    if (str_ends_ext(e->name, "BMP"))
        { open_with("/APPS/IMGVIEW.QEX", full_path); return; }
    if (str_ends_ext(e->name, "TXT"))
        { open_with("/APPS/NOTEPAD.QEX", full_path); return; }
    if (str_ends_ext(e->name, "QEX"))
        { spawn_qex(full_path); return; }
}
int main(void)
{
    comp_pid    = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    storage_pid = qk_get_server_pid(QK_SERVER_STORAGE);
    if (comp_pid < 0 || storage_pid < 0) return 1;
    void *font_addr = 0;
    if (qk_map_font_atlas(&font_addr) == 0)
        font_atlas_init((uint8_t *)font_addr);
    void *icons_addr = 0;
    if (qk_map_icons_atlas(&icons_addr) == 0 && icons_addr) {
        gui_icons = (AtlasFont *)malloc(sizeof(AtlasFont));
        font_atlas_init_for_ptr(gui_icons, (uint8_t *)icons_addr);
    }
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type  = MSG_WIN_CREATE;
    msg.data1 = WIN_W;
    msg.data2 = WIN_H;
    strncpy((char *)msg.payload, "Files", 63);
    qk_sys_ipc_send(comp_pid, &msg);
    while (1) { qk_ipc_recv(&msg); if (msg.type == MSG_WIN_CREATED) break; }
    win_id = (int)msg.data1;
    gfx = (uint32_t *)qk_win_map_buffer(comp_pid, win_id, &gfx_w, &gfx_h);
    qk_set_proc_name(-1, "filemgr");
    dir_depth = 0;
    dir_stack[0] = 0;
    cur_cluster = 0;
    rebuild_path();
    load_dir();
    render();
    while (1) {
        qk_ipc_recv(&msg);
        int needs_render = 0;
        do {
            if (msg.type == MSG_WIN_RESIZED) {
                uint32_t *ng = (uint32_t *)qk_win_map_buffer(comp_pid, win_id, &gfx_w, &gfx_h);
                if (ng) gfx = ng;
                needs_render = 1;
            } else if (msg.type == MSG_MOUSE_EVENT) {
                Event *ev = (Event *)msg.payload;
                if (ev->type == EVENT_MOUSE_SCROLL) {
                    grid_scroll += ev->scroll;
                    needs_render = 1;
                } else if (ev->type == EVENT_MOUSE_MOVE) {
                    int mx = ev->x;
                    int my = ev->y - TITLE_BAR_HEIGHT;
                    int new_hsb = -1, new_hg = -1;
                    if (mx >= 0 && mx < SIDEBAR_W && my >= HEADER_H) {
                        int sy2 = HEADER_H + 8;
                        for (int i = 0; i < SB_N; i++) {
                            if (sb[i].kind == 1) { sy2 += 12; continue; }
                            if (!sb[i].label) continue;
                            if (my >= sy2 && my < sy2 + SB_ITEM_H)
                                new_hsb = i;
                            sy2 += SB_ITEM_H;
                        }
                    }
                    if (mx >= CONTENT_X && mx < CONTENT_X + GCOLS * GITEM_W &&
                        my >= CONTENT_Y) {
                        int col2 = (mx - CONTENT_X) / GITEM_W;
                        int rows_vis2 = (gfx_h - HEADER_H) / GITEM_H;
                        int row2 = (my - CONTENT_Y) / GITEM_H;
                        if (row2 >= 0 && row2 <= rows_vis2) {
                            int idx2 = (row2 + grid_scroll) * GCOLS + col2;
                            if (idx2 >= 0 && idx2 < entry_count)
                                new_hg = idx2;
                        }
                    }
                    if (new_hsb != hover_sb || new_hg != hover_grid) {
                        hover_sb   = new_hsb;
                        hover_grid = new_hg;
                        needs_render = 1;
                    }
                } else if (ev->type == EVENT_MOUSE_DOWN && ev->button == MOUSE_BTN_LEFT) {
                    int cx = ev->x;
                    int cy = ev->y - TITLE_BAR_HEIGHT;
                    if (cy >= 0 && cy < HEADER_H &&
                        cx >= CONTENT_X + 4 && cx < CONTENT_X + 38 &&
                        dir_depth > 0) {
                        dir_depth--;
                        cur_cluster = dir_stack[dir_depth];
                        rebuild_path();
                        load_dir();
                        needs_render = 1;
                    } else if (cx >= 0 && cx < SIDEBAR_W && cy >= HEADER_H) {
                        int sy = HEADER_H + 8;
                        for (int i = 0; i < SB_N; i++) {
                            if (sb[i].kind == 1) { sy += 12; continue; }
                            if (!sb[i].label) continue;
                            if (cy >= sy && cy < sy + SB_ITEM_H) {
                                if (navigate_to_path(sb[i].path)) {
                                    load_dir();
                                    needs_render = 1;
                                }
                                break;
                            }
                            sy += SB_ITEM_H;
                        }
                    } else if (cx >= CONTENT_X && cx < CONTENT_X + GCOLS * GITEM_W &&
                               cy >= CONTENT_Y) {
                        int col = (cx - CONTENT_X) / GITEM_W;
                        int row_s = (cy - CONTENT_Y) / GITEM_H;
                        int idx = (row_s + grid_scroll) * GCOLS + col;
                        if (idx >= 0 && idx < entry_count) {
                            if (idx == selected) {
                                if (needs_render) render();
                                needs_render = 0;
                                launch_entry(idx);
                            } else {
                                selected = idx;
                                needs_render = 1;
                            }
                        }
                    }
                }
            } else if (msg.type == MSG_KEY_EVENT) {
                uint8_t sc = (uint8_t)(msg.data1 & 0x7F);
                bool released = (msg.data1 & 0x80) != 0;
                if (!released) {
                    int rows_total_k = (entry_count + GCOLS - 1) / GCOLS;
                    int rows_vis_k   = (gfx_h - HEADER_H) / GITEM_H;
                    int max_s = rows_total_k - rows_vis_k;
                    if (max_s < 0) max_s = 0;
                    if (sc == 0x4B) { if (selected > 0) selected--; needs_render = 1; }
                    else if (sc == 0x4D) { if (selected < entry_count - 1) selected++; needs_render = 1; }
                    else if (sc == 0x48) { if (selected >= GCOLS) selected -= GCOLS; needs_render = 1; }
                    else if (sc == 0x50) { if (selected + GCOLS < entry_count) selected += GCOLS; needs_render = 1; }
                    else if (sc == 0x1C) {
                        if (selected >= 0) { if (needs_render) render(); needs_render = 0; launch_entry(selected); }
                    } else if (sc == 0x0E) {
                        if (dir_depth > 0) {
                            dir_depth--;
                            cur_cluster = dir_stack[dir_depth];
                            rebuild_path();
                            load_dir();
                            needs_render = 1;
                        }
                    }
                    if (needs_render && selected >= 0) {
                        int sel_row = selected / GCOLS;
                        if (sel_row < grid_scroll) grid_scroll = sel_row;
                        if (sel_row >= grid_scroll + rows_vis_k)
                            grid_scroll = sel_row - rows_vis_k + 1;
                        if (grid_scroll > max_s) grid_scroll = max_s;
                        if (grid_scroll < 0) grid_scroll = 0;
                    }
                }
            }
        } while (qk_ipc_recv_nb(&msg) == 0);
        if (needs_render) render();
    }
    return 0;
}
