#include "abi/events.h"
#include "abi/ipc.h"
#include "stdbool.h"
#include "stdlib.h"
#include "user/libui/font_atlas.h"
#include "user/libui/gfx.h"
#include "user/libui/icons_def.h"
#include "user/libc/libapi.h"
#include "user/libui/window.h"

#define WIN_W     480
#define WIN_H     470

#define OUTER_PAD        14
#define SEARCH_H         60
#define ITEM_H           56
#define ICON_SZ          18
#define INNER_PAD        16
#define QUERY_MAX        64
#define MAX_APPS         16
#define MAX_FILE_RESULTS 32

#define C_BG       0xFF242424u
#define C_SRCH_BG  0xFF1C1C1Cu
#define C_SEL      0xFF353535u
#define C_ACCENT   0xFF9A9A9Au
#define C_TEXT     0xFFD0D0D0u
#define C_DIM      0xFF787878u
#define C_ICON     0xFFAAAAAAu
#define C_SEP      0xFF363636u
#define C_CURSOR   0xFFBBBBBBu
#define C_ICON_BG  0xFF333333u
#define ICON_IMAGE 0xEA06u

#define KEY_MOD_SHIFT 0x01u
#define KEY_MOD_CAPS  0x08u

typedef struct {
    const char *name;
    const char *desc;
    const char *path;
    uint32_t    icon;
} AppEntry;

static const AppEntry apps[] = {
    {"Terminal",        "Shell & command line",    "/PRG/TTYSH.QEX",    ICON_TERMINAL  },
    {"File Manager",    "Browse files & folders",  "/PRG/FILEMGR.QEX",  ICON_FOLDER    },
    {"Notepad",         "Text editor",             "/PRG/NOTEPAD.QEX",  ICON_FILE_TEXT },
    {"Paint",           "Draw & sketch",           "/PRG/PAINT.QEX",    ICON_FOCUS     },
    {"Image Viewer",    "View images",             "/PRG/IMGVIEW.QEX",  ICON_IMAGE     },
    {"Process Manager", "System activity & stats", "/PRG/PROCMAN.QEX",  ICON_MONITOR   },
};
#define N_APPS ((int)(sizeof(apps) / sizeof(apps[0])))

static int      comp_pid    = -1;
static int      storage_pid = -1;
static int      win_id      = -1;
static uint32_t *gfx        = 0;
static int      gfx_w       = 0, gfx_h = 0;

static char query[QUERY_MAX] = {0};
static int  qlen = 0;

static int results[MAX_APPS];
static int nresults = 0;
static int sel      = 0;
static int scroll   = 0;

typedef struct {
    char     name[64];
    char     path[96];
    uint32_t cluster;
    bool     is_dir;
} FileEntry;

static FileEntry file_results[MAX_FILE_RESULTS];
static int       nfile_results = 0;
static bool      file_mode     = false;

static bool calc_mode       = false;
static char calc_result[32] = {0};

static const char *_cp;
static bool        _ce;

static long _cfactor(void);
static long _cterm(void);
static long _cexpr(void);

static void _cskip(void) { while (*_cp == ' ') _cp++; }

static long _cfactor(void) {
    _cskip();
    if (*_cp == '(') {
        _cp++;
        long v = _cexpr();
        _cskip();
        if (*_cp == ')') _cp++; else _ce = true;
        return v;
    }
    if (*_cp == '-') { _cp++; return -_cfactor(); }
    if (*_cp == '+') { _cp++; return  _cfactor(); }
    if (*_cp >= '0' && *_cp <= '9') {
        long v = 0;
        while (*_cp >= '0' && *_cp <= '9') v = v * 10 + (*_cp++ - '0');
        return v;
    }
    _ce = true; return 0;
}
static long _cterm(void) {
    long v = _cfactor();
    while (!_ce) {
        _cskip();
        if      (*_cp == '*') { _cp++; v *= _cfactor(); }
        else if (*_cp == '/') { _cp++; long d = _cfactor(); if (!d) { _ce=true; return 0; } v /= d; }
        else if (*_cp == '%') { _cp++; long d = _cfactor(); if (!d) { _ce=true; return 0; } v %= d; }
        else break;
    }
    return v;
}
static long _cexpr(void) {
    long v = _cterm();
    while (!_ce) {
        _cskip();
        if      (*_cp == '+') { _cp++; v += _cterm(); }
        else if (*_cp == '-') { _cp++; v -= _cterm(); }
        else break;
    }
    return v;
}

static bool try_calc(void) {

    bool has_op = false;
    for (int i = 0; query[i]; i++) {
        char c = query[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return false;
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%') has_op = true;
    }
    if (!has_op) return false;
    _cp = query; _ce = false;
    long r = _cexpr();
    _cskip();
    if (_ce || *_cp != '\0') return false;

    if (r < 0) { calc_result[0] = '-'; int_to_str((int)(-r), calc_result + 1, 10); }
    else        int_to_str((int)r, calc_result, 10);
    return true;
}
static int      current_win_h  = SEARCH_H;
static int      anim_from_h    = SEARCH_H;
static int      anim_to_h      = SEARCH_H;
static uint32_t anim_start_ms  = 0;
static bool     animating      = false;
#define ANIM_MS 120u

static int desired_win_h(void) {
    int n = file_mode ? nfile_results : calc_mode ? 1 : nresults;
    if (qlen == 0 || n == 0) return SEARCH_H;
    int h = SEARCH_H + 12 + n * ITEM_H;
    if (h > WIN_H) h = WIN_H;
    return h;
}

static void maybe_resize(void) {
    int want = desired_win_h();
    if (want == anim_to_h) return;
    anim_from_h   = current_win_h;
    anim_to_h     = want;
    anim_start_ms = qk_get_uptime();
    animating     = true;
}

static void render(void);

static bool anim_tick(void) {
    if (!animating) return false;
    uint32_t elapsed = qk_get_uptime() - anim_start_ms;
    if (elapsed > ANIM_MS) elapsed = ANIM_MS;


    uint32_t t    = elapsed * 256u / ANIM_MS;
    uint32_t ease = t * (512u - t) / 256u;
    int new_h = anim_from_h + (int)((anim_to_h - anim_from_h) * (int)ease / 256);

    if (elapsed >= ANIM_MS) {
        new_h     = anim_to_h;
        animating = false;
    }

    if (new_h == current_win_h) return false;

    message_t rm; memset(&rm, 0, sizeof(rm));
    rm.type  = MSG_WIN_RESIZE;
    rm.data1 = (uint32_t)win_id;
    rm.data2 = WIN_W;
    rm.data3 = (uint32_t)new_h;
    qk_sys_ipc_send(comp_pid, &rm);
    while (1) { qk_ipc_recv(&rm); if (rm.type == MSG_WIN_RESIZED) break; }
    gfx = (uint32_t *)qk_win_map_buffer(comp_pid, win_id, &gfx_w, &gfx_h);
    current_win_h = new_h;
    render();
    return false;
}

static bool icontains(const char *hay, const char *needle) {
    if (!needle[0]) return true;
    for (int i = 0; hay[i]; i++) {
        bool ok = true;
        for (int j = 0; needle[j]; j++) {
            char h = hay[i + j], n = needle[j];
            if (h >= 'A' && h <= 'Z') h += 32;
            if (n >= 'A' && n <= 'Z') n += 32;
            if (!h || h != n) { ok = false; break; }
        }
        if (ok) return true;
    }
    return false;
}

static void filter(void) {
    nresults  = 0;
    sel       = 0;
    scroll    = 0;
    calc_mode = false;
    for (int i = 0; i < N_APPS && nresults < MAX_APPS; i++) {
        if (icontains(apps[i].name, query) || icontains(apps[i].desc, query))
            results[nresults++] = i;
    }
    if (nresults == 0 && qlen > 0)
        calc_mode = try_calc();
}

static void file_filter(void) {
    nfile_results = 0;
    sel           = 0;
    scroll        = 0;

    const char *needle = (qlen > 1) ? query + 1 : "";


#define FS_BFS_MAX 64
    static uint32_t q_cls[FS_BFS_MAX];
    static char     q_path[FS_BFS_MAX][96];
    int qh = 0, qt = 0;

    q_cls[0]    = 0;
    q_path[0][0] = '/';
    q_path[0][1] = 0;
    qt = 1;

    while (qh != qt) {
        uint32_t cls = q_cls[qh];
        char dir_path[96];
        int dp = 0;
        while (q_path[qh][dp] && dp < 95) { dir_path[dp] = q_path[qh][dp]; dp++; }
        dir_path[dp] = 0;
        qh = (qh + 1) % FS_BFS_MAX;

        message_t req, resp;
        memset(&req, 0, sizeof(req));
        req.type  = MSG_DIR_REQ;
        req.data1 = cls;
        req.data3 = 0;
        qk_sys_ipc_send(storage_pid, &req);

        while (1) {
            qk_ipc_recv(&resp);
            if (resp.type != MSG_DIR_REPLY) continue;
            char *ptr = (char *)resp.payload;
            while (*ptr) {
                FileEntry e;
                e.is_dir = (*ptr == '1');
                while (*ptr && *ptr != '|') ptr++;
                if (*ptr == '|') ptr++;
                uint32_t cl = 0;
                while (*ptr >= '0' && *ptr <= '9') { cl = cl * 10 + (uint32_t)(*ptr - '0'); ptr++; }
                e.cluster = cl;
                if (*ptr == '|') ptr++;
                int ni = 0;
                while (*ptr && *ptr != '\n' && ni < 63) e.name[ni++] = *ptr++;
                e.name[ni] = 0;
                if (*ptr == '\n') ptr++;
                if (ni == 0 || (e.name[0] == '.' && (ni == 1 || (ni == 2 && e.name[1] == '.')))) continue;


                int pl = 0;
                while (dir_path[pl] && pl < 94) { e.path[pl] = dir_path[pl]; pl++; }
                if (pl > 1 && e.path[pl - 1] != '/') e.path[pl++] = '/';
                for (int k = 0; e.name[k] && pl < 95; k++) e.path[pl++] = e.name[k];
                e.path[pl] = 0;


                if (e.is_dir) {
                    int nq = (qt + 1) % FS_BFS_MAX;
                    if (nq != qh) {
                        q_cls[qt] = e.cluster;
                        int dl = 0;
                        while (e.path[dl] && dl < 95) { q_path[qt][dl] = e.path[dl]; dl++; }
                        q_path[qt][dl] = 0;
                        qt = nq;
                    }
                }


                if (nfile_results < MAX_FILE_RESULTS &&
                    (!needle[0] || icontains(e.name, needle)))
                    file_results[nfile_results++] = e;
            }
            if (resp.data1 == 0) break;
        }
    }
}

static void fill(int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0 || x >= gfx_w || y >= gfx_h) return;
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

static void fill_rd(int x, int y, int w, int h, int r, uint32_t c) {
    gfx_context_t ctx = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
    gfx_rect_rounded_fill(&ctx, x, y, w, h, r, c);
}

static void dstr(int x, int y, const char *s, uint32_t c) {
    gfx_context_t ctx = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
    draw_string_vector(&ctx, x, y + 15, s, c);
}

static void dicon(int x, int y, uint32_t id, uint32_t c) {
    if (!gui_icons) return;
    gfx_context_t ctx = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
    draw_icon_sized(&ctx, gui_icons, x, y, id, c, ICON_SZ);
}

static void render(void) {
    if (!gfx) return;
    int fh = get_font_height();
    if (fh <= 0) return;


    fill(0, 0, gfx_w, gfx_h, C_BG);



    int ic_r  = 18;
    int ic_cx = OUTER_PAD + 2 + ic_r;
    int ic_cy = SEARCH_H / 2;
    fill_rd(ic_cx - ic_r, ic_cy - ic_r, ic_r * 2, ic_r * 2, ic_r, 0xFF383838u);
    {
        gfx_context_t ctx = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
        draw_icon_sized(&ctx, gui_icons, ic_cx - 7, ic_cy - 7,
                        ICON_SEARCH, 0xFFD7D7D7u, 14);
    }


    int tx = ic_cx + ic_r + 16;
    int ty = (SEARCH_H - fh) / 2;
    if (qlen > 0) {
        dstr(tx, ty, query, C_TEXT);

        bool cursor_on = (qk_get_uptime() % 1000u) < 500u;
        if (cursor_on) {
            int qw = get_string_width(query);
            fill(tx + qw + 2, ty + 4, 2, fh - 4, C_CURSOR);
        }
    } else {
        dstr(tx, ty, "Search apps, or !name for files", C_DIM);
    }


    int n_shown = file_mode ? nfile_results : calc_mode ? 1 : nresults;
    if (qlen > 0 && n_shown > 0 && !calc_mode) {
        char cnt[8];
        int_to_str(n_shown, cnt, 10);
        int cw = get_string_width(cnt);
        int bh = fh + 8;
        int bw = cw + 14;
        if (bw < bh) bw = bh;
        int bx = gfx_w - OUTER_PAD - bw;
        int by = (SEARCH_H - bh) / 2;
        fill_rd(bx, by, bw, bh, bh / 2, C_ICON_BG);
        dstr(bx + (bw - cw) / 2, by + 4, cnt, C_DIM);
    }

    if (n_shown == 0 || qlen == 0) {
        if (qlen > 0 && n_shown == 0) {
            int mw = get_string_width("No results");
            dstr((gfx_w - mw) / 2, SEARCH_H + 30, "No results", C_DIM);
        }
        qk_win_dirty(comp_pid);
        return;
    }


    fill(OUTER_PAD, SEARCH_H, gfx_w - OUTER_PAD * 2, 1, C_SEP);

    if (calc_mode) {

        int iy = SEARCH_H + 6;

        int rw  = get_string_width(calc_result);
        int eqw = get_string_width("=");
        int ty  = iy + (ITEM_H - fh) / 2;
        dstr(OUTER_PAD + INNER_PAD, ty, "=", C_DIM);
        dstr(gfx_w - OUTER_PAD - INNER_PAD - rw, ty, calc_result, C_TEXT);
        qk_win_dirty(comp_pid);
        (void)eqw;
        return;
    }


    fill(OUTER_PAD, SEARCH_H, gfx_w - OUTER_PAD * 2, 1, C_SEP);


    int list_y  = SEARCH_H + 6;
    int avail_h = gfx_h - list_y - 6;
    int max_vis = avail_h / ITEM_H;
    if (max_vis < 1) max_vis = 1;

    if (sel < scroll) scroll = sel;
    if (sel >= scroll + max_vis) scroll = sel - max_vis + 1;

    if (file_mode) {

        for (int vi = 0; vi < max_vis && (scroll + vi) < nfile_results; vi++) {
            FileEntry *fe = &file_results[scroll + vi];
            int iy       = list_y + vi * ITEM_H;
            bool is_sel  = (scroll + vi) == sel;

            if (is_sel)
                fill_rd(OUTER_PAD, iy + 3, gfx_w - OUTER_PAD * 2, ITEM_H - 6, 8, C_SEL);

            int ico_cx = OUTER_PAD + INNER_PAD + 16;
            int ico_cy = iy + ITEM_H / 2;
            fill_rd(ico_cx - 16, ico_cy - 16, 32, 32, 8,
                    is_sel ? C_SEL : C_ICON_BG);
            uint32_t ico = fe->is_dir ? ICON_FOLDER : ICON_FILE_TEXT;
            dicon(ico_cx - ICON_SZ / 2, ico_cy - ICON_SZ / 2,
                  ico, is_sel ? C_TEXT : C_ICON);

            int name_x = OUTER_PAD + INNER_PAD + 40;
            dstr(name_x, iy + (ITEM_H / 2) - fh - 1,
                 fe->name, is_sel ? C_TEXT : 0xFFBBBBBBu);
            dstr(name_x, iy + ITEM_H / 2 + 2, fe->path, C_DIM);

            if (is_sel)
                dicon(gfx_w - OUTER_PAD - INNER_PAD - ICON_SZ,
                      iy + (ITEM_H - ICON_SZ) / 2,
                      ICON_ARROW_RIGHT, C_ACCENT);
        }

        if (nfile_results > max_vis) {
            int track_h = max_vis * ITEM_H;
            int thumb_h = (max_vis * track_h) / nfile_results;
            if (thumb_h < 16) thumb_h = 16;
            int range   = nfile_results - max_vis;
            int thumb_y = list_y + (scroll * (track_h - thumb_h)) / (range ? range : 1);
            fill_rd(gfx_w - 5, thumb_y + 4, 3, thumb_h - 8, 1, C_DIM);
        }
    } else {

        for (int vi = 0; vi < max_vis && (scroll + vi) < nresults; vi++) {
            int idx     = results[scroll + vi];
            int iy      = list_y + vi * ITEM_H;
            bool is_sel = (scroll + vi) == sel;

            if (is_sel)
                fill_rd(OUTER_PAD, iy + 3, gfx_w - OUTER_PAD * 2, ITEM_H - 6, 8, C_SEL);

            int ico_cx = OUTER_PAD + INNER_PAD + 16;
            int ico_cy = iy + ITEM_H / 2;
            fill_rd(ico_cx - 16, ico_cy - 16, 32, 32, 8,
                    is_sel ? C_SEL : C_ICON_BG);
            dicon(ico_cx - ICON_SZ / 2, ico_cy - ICON_SZ / 2,
                  apps[idx].icon, is_sel ? C_TEXT : C_ICON);

            int name_x = OUTER_PAD + INNER_PAD + 40;
            dstr(name_x, iy + (ITEM_H / 2) - fh - 1,
                 apps[idx].name, is_sel ? C_TEXT : 0xFFBBBBBBu);
            dstr(name_x, iy + ITEM_H / 2 + 2, apps[idx].desc, C_DIM);

            if (is_sel)
                dicon(gfx_w - OUTER_PAD - INNER_PAD - ICON_SZ,
                      iy + (ITEM_H - ICON_SZ) / 2,
                      ICON_ARROW_RIGHT, C_ACCENT);
        }

        if (nresults > max_vis) {
            int track_h = max_vis * ITEM_H;
            int thumb_h = (max_vis * track_h) / nresults;
            if (thumb_h < 16) thumb_h = 16;
            int range   = nresults - max_vis;
            int thumb_y = list_y + (scroll * (track_h - thumb_h)) / (range ? range : 1);
            fill_rd(gfx_w - 5, thumb_y + 4, 3, thumb_h - 8, 1, C_DIM);
        }
    }

    qk_win_dirty(comp_pid);
}

static int spawn_app(const char *qex_path) {
    message_t req, resp;
    memset(&req, 0, sizeof(req));
    req.type  = MSG_FILE_OPEN;
    req.data1 = 0xFFFF;
    strncpy((char *)req.payload, qex_path, sizeof(req.payload) - 1);
    qk_sys_ipc_send(storage_pid, &req);
    while (1) { qk_ipc_recv(&resp); if (resp.sender_pid == storage_pid) break; }
    if (resp.type != MSG_FILE_SIZE || resp.data1 == 0) return -1;
    uint32_t size = resp.data1;
    uint8_t *buf  = (uint8_t *)malloc(size);
    if (!buf) return -1;
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
        memcpy(buf + off, resp.payload, got);
        off += got;
    }
    int pid = (off > 0) ? qk_spawn(buf, off) : -1;
    free(buf);
    return pid;
}

static void launch(int app_idx) {
    spawn_app(apps[app_idx].path);
}

static bool ext_is(const char *name, int nl, const char *ext) {

    if (nl < 5) return false;
    if (name[nl-4] != '.') return false;
    char a = name[nl-3], b = name[nl-2], c = name[nl-1];
    if (a >= 'a' && a <= 'z') a -= 32;
    if (b >= 'a' && b <= 'z') b -= 32;
    if (c >= 'a' && c <= 'z') c -= 32;
    return (a == ext[0] && b == ext[1] && c == ext[2]);
}

static void launch_file(int fi) {
    FileEntry *fe = &file_results[fi];

    if (fe->is_dir) {
        int pid = spawn_app("/PRG/FILEMGR.QEX");
        if (pid > 0) {
            message_t nav;
            memset(&nav, 0, sizeof(nav));
            nav.type = MSG_FILEMGR_NAVIGATE;
            strncpy((char *)nav.payload, fe->path, sizeof(nav.payload) - 1);
            qk_sys_ipc_send(pid, &nav);
        }
        return;
    }

    int nl = 0;
    while (fe->name[nl]) nl++;

    if (ext_is(fe->name, nl, "QEX")) {

        spawn_app(fe->path);
        return;
    }


    if (ext_is(fe->name, nl, "BMP") ||
        ext_is(fe->name, nl, "PNG") ||
        ext_is(fe->name, nl, "JPG")) {
        int pid = spawn_app("/PRG/IMGVIEW.QEX");
        if (pid > 0) {
            message_t m;
            memset(&m, 0, sizeof(m));
            m.type = MSG_FILE_OPEN;
            strncpy((char *)m.payload, fe->path, sizeof(m.payload) - 1);
            qk_sys_ipc_send(pid, &m);
        }
        return;
    }


    if (ext_is(fe->name, nl, "TXT") ||
        ext_is(fe->name, nl, "LOG") ||
        ext_is(fe->name, nl, "INI") ||
        ext_is(fe->name, nl, "CFG")) {
        int pid = spawn_app("/PRG/NOTEPAD.QEX");
        if (pid > 0) {
            message_t m;
            memset(&m, 0, sizeof(m));
            m.type = MSG_FILE_OPEN;
            strncpy((char *)m.payload, fe->path, sizeof(m.payload) - 1);
            qk_sys_ipc_send(pid, &m);
        }
        return;
    }


    int pid = spawn_app("/PRG/FILEMGR.QEX");
    if (pid > 0) {

        char parent[96];
        int i = 0;
        while (fe->path[i]) i++;
        while (i > 0 && fe->path[i] != '/') i--;
        if (i == 0) { parent[0] = '/'; parent[1] = 0; }
        else { int k = 0; while (k < i && k < 95) { parent[k] = fe->path[k]; k++; } parent[k] = 0; }
        message_t nav;
        memset(&nav, 0, sizeof(nav));
        nav.type = MSG_FILEMGR_NAVIGATE;
        strncpy((char *)nav.payload, parent, sizeof(nav.payload) - 1);
        qk_sys_ipc_send(pid, &nav);
    }
}

static char sc_to_char(uint8_t code, uint32_t mods) {
    bool sh  = (mods & KEY_MOD_SHIFT) != 0;
    bool cap = (mods & KEY_MOD_CAPS)  != 0;
    const char *r1  = "1234567890-=",  *r1s = "!@#$%^&*()_+";
    const char *r2  = "qwertyuiop[]",  *r2s = "QWERTYUIOP{}";
    const char *r3  = "asdfghjkl;'`",  *r3s = "ASDFGHJKL:\"~";
    const char *r4  = "zxcvbnm,./",    *r4s = "ZXCVBNM<>?";
    bool up = sh ^ cap;
    if (code >= 0x02 && code <= 0x0D) return sh  ? r1s[code - 0x02] : r1[code - 0x02];
    if (code >= 0x10 && code <= 0x1B) return up  ? r2s[code - 0x10] : r2[code - 0x10];
    if (code >= 0x1E && code <= 0x29) return (code <= 0x26 ? up : sh) ? r3s[code - 0x1E] : r3[code - 0x1E];
    if (code >= 0x2C && code <= 0x35) return (code <= 0x32 ? up : sh) ? r4s[code - 0x2C] : r4[code - 0x2C];
    if (code == 0x39) return ' ';
    return 0;
}

int main(void) {
    comp_pid    = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    storage_pid = qk_get_server_pid(QK_SERVER_STORAGE);
    if (comp_pid < 0 || storage_pid < 0) return 1;

    void *font_addr = 0;
    if (qk_map_font_atlas(&font_addr) == 0)
        font_atlas_init((uint8_t *)font_addr);

    void *icons_addr = 0;
    if (qk_map_icons_atlas(&icons_addr) == 0) {
        gui_icons = (AtlasFont *)malloc(sizeof(AtlasFont));
        if (gui_icons)
            font_atlas_init_for_ptr(gui_icons, (uint8_t *)icons_addr);
    }


    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type  = MSG_WIN_CREATE;
    msg.data1 = WIN_W;
    msg.data2 = SEARCH_H;
    msg.data3 = WIN_FLAG_NO_DECOR;
    strncpy((char *)msg.payload, "Launcher", 63);
    qk_sys_ipc_send(comp_pid, &msg);
    while (1) {
        qk_ipc_recv(&msg);
        if (msg.type == MSG_WIN_CREATED) break;
    }
    win_id = (int)msg.data1;

    gfx = (uint32_t *)qk_win_map_buffer(comp_pid, win_id, &gfx_w, &gfx_h);
    if (!gfx) return 1;

    filter();
    maybe_resize();
    render();

    uint32_t mods        = 0;
    bool     last_cursor = false;
    uint32_t last_frame  = 0;
#define FRAME_MS 30u

    while (1) {
        message_t ev;
        bool dirty = false;

        while (qk_ipc_recv_nb(&ev) == 0) {
            if (ev.type == MSG_WIN_RESIZED) {
                gfx   = (uint32_t *)qk_win_map_buffer(comp_pid, win_id, &gfx_w, &gfx_h);
                dirty = true;
            } else if (ev.type == MSG_KEY_EVENT) {
                Event *e   = (Event *)ev.payload;
                uint8_t sc = (uint8_t)(e->key & 0x7Fu);
                if (e->type == EVENT_KEY_PRESS) {
                    if (sc == 0x2A || sc == 0x36) mods |=  KEY_MOD_SHIFT;
                    if (sc == 0x3A)                mods ^=  KEY_MOD_CAPS;

                    int cur_n = file_mode ? nfile_results : calc_mode ? 0 : nresults;
                    if (sc == 0x01) {
                        if (qlen > 0) {
                            qlen = 0; query[0] = 0;
                            file_mode = false; calc_mode = false;
                            filter(); maybe_resize(); dirty = true;
                        } else {
                            qk_sys_exit(0);
                        }
                    } else if (sc == 0x1C) {
                        if (cur_n > 0) {
                            if (file_mode) launch_file(sel);
                            else           launch(results[sel]);
                            qk_sys_exit(0);
                        }
                    } else if (sc == 0x48) {
                        if (sel > 0) { sel--; dirty = true; }
                    } else if (sc == 0x50) {
                        if (sel < cur_n - 1) { sel++; dirty = true; }
                    } else if (sc == 0x0E) {
                        if (qlen > 0) {
                            query[--qlen] = 0;
                            bool new_fm = (qlen > 0 && query[0] == '!');
                            if (new_fm != file_mode) { file_mode = new_fm; sel = 0; scroll = 0; }
                            if (file_mode) file_filter(); else filter();
                            maybe_resize();
                            dirty = true;
                        }
                    } else {
                        char c = sc_to_char(sc, mods);
                        if (c && qlen < QUERY_MAX - 1) {
                            query[qlen++] = c;
                            query[qlen]   = 0;
                            bool new_fm = (query[0] == '!');
                            if (new_fm != file_mode) { file_mode = new_fm; sel = 0; scroll = 0; }
                            if (file_mode) file_filter(); else filter();
                            maybe_resize();
                            dirty = true;
                        }
                    }
                } else if (e->type == EVENT_KEY_RELEASE) {
                    if (sc == 0x2A || sc == 0x36) mods &= ~KEY_MOD_SHIFT;
                }
            } else if (ev.type == MSG_MOUSE_EVENT) {
                Event *e = (Event *)ev.payload;
                int cur_n = file_mode ? nfile_results : calc_mode ? 0 : nresults;
                if (e->type == EVENT_MOUSE_DOWN && e->button == MOUSE_BTN_LEFT) {
                    int list_y  = SEARCH_H + 6;
                    int avail_h = gfx_h - list_y - 6;
                    int max_vis = avail_h / ITEM_H;
                    int vy      = (e->y - list_y) / ITEM_H;
                    if (vy >= 0 && vy < max_vis && (scroll + vy) < cur_n) {
                        if ((scroll + vy) == sel) {
                            if (file_mode) launch_file(sel);
                            else           launch(results[sel]);
                            qk_sys_exit(0);
                        }
                        sel   = scroll + vy;
                        dirty = true;
                    }
                } else if (e->type == EVENT_MOUSE_SCROLL) {
                    if (e->scroll < 0 && sel > 0)           { sel--; dirty = true; }
                    if (e->scroll > 0 && sel < cur_n - 1)   { sel++; dirty = true; }
                }
            }
        }

        uint32_t now       = qk_get_uptime();
        bool     frame_due = (now - last_frame) >= FRAME_MS;


        if (animating && frame_due) {
            anim_tick();
            last_frame = now;
        }


        bool cursor_on = (now % 1000u) < 500u;
        if (cursor_on != last_cursor) {
            last_cursor = cursor_on;
            dirty       = true;
        }

        if (dirty && frame_due) {
            render();
            last_frame = now;
        } else if (!dirty && !animating) {
            qk_yield();
        }
    }

    return 0;
}
