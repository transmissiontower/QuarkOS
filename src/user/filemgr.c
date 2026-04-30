// SPDX-License-Identifier: MIT
/*
 * src/user/filemgr.c - Graphical file manager with sidebar navigation and grid view
 */
#include "kernel/ipc.h"
#include "lib/stdbool.h"
#include "lib/stdlib.h"
#include "user/lib/font_atlas.h"
#include "user/lib/gfx.h"
#include "user/lib/icons_def.h"
#ifndef ICON_IMAGE
#define ICON_IMAGE 0xEA06
#endif
#include "user/lib/libapi.h"
#include "user/lib/window.h"
#define WIN_W 900
#define WIN_H 580
#define HEADER_H 34
#define SIDEBAR_W 185
#define CONTENT_X SIDEBAR_W
#define CONTENT_Y HEADER_H
#define SB_ITEM_H 28
#define SB_ICON_SZ 16
#define SCROLLBAR_W 8
#define GCOLS 4
#define GITEM_W 160
#define GITEM_H 110
#define GICON_W 100
#define GICON_H 80
#define CB_HEADER 0xFF242424u
#define CB_SIDEBAR 0xFF202020u
#define CB_CONTENT 0xFF1A1A1Au
#define CB_SEL 0xFF2E2E2Eu
#define CB_HOVER 0xFF272727u
#define C_SEP 0xFF333333u
#define C_TEXT 0xFFDDDDDDu
#define C_DIM 0xFF6B7C7Cu
#define C_ACCENT 0xFF59B2D4u
#define CF_BODY 0xFF78AEE8u
#define CF_TOP 0xFF5A8FD0u
#define CF_TAB 0xFF1F5DA8u
#define CF_SHADE 0xFF96C3F0u
#define CF_SHADOW 0xFF2A4E8Au
#define CF_FILE 0xFFDCE3EAu
#define CF_FFOLD 0xFFAAB6C2u
#define CF_EXE 0xFF80CC80u
#define CF_EFOLD 0xFF52A452u
#define C_SBICO 0xFF8BAAB2u
static int comp_pid = -1;
static int storage_pid = -1;
static uint32_t *gfx = 0;
static int gfx_w = 0;
static int gfx_h = 0;
static int win_id = -1;
#define MAX_DEPTH 12
static uint32_t dir_stack[MAX_DEPTH];
static char dir_names[MAX_DEPTH][16];
static int dir_depth = 0;
static uint32_t cur_cluster = 0;
static char cur_path[128] = "/";
static int fm_disk_idx = 0;
#define MAX_ENTRIES 128
typedef struct {
    int is_dir;
    uint32_t cluster;
    char name[64];
} fm_entry_t;
static fm_entry_t entries[MAX_ENTRIES];
static int entry_count = 0;
#define STASH_SIZE 8
static message_t stash_buf[STASH_SIZE];
static int stash_count = 0;
static int selected = -1;
static int grid_scroll = 0;
static int hover_sb = -1;
static int hover_grid = -1;
static int path_editing = 0;
static char edit_buf[128];
static int edit_len = 0;
static int search_active = 0;
static char search_buf[64];
static int search_len = 0;
static char entry_paths[MAX_ENTRIES][96];
static fm_entry_t search_cache[MAX_ENTRIES];
static char search_cache_paths[MAX_ENTRIES][96];
static int search_cache_count = 0;

static int picker_mode = 0;
static int requester_pid = -1;
static char picker_filename[64];
static int picker_fn_len = 0;
static int picker_fn_editing = 0;
static int pfn_cursor = 0;
static int pfn_sel_start = -1;
static int pfn_sel_end = -1;
static bool pfn_mouse_sel = false;

static void clip_set_fm(const char *text, int len) {
    message_t m;
    memset(&m, 0, sizeof(m));
    m.type = MSG_CLIP_SET;
    if (len > 4095)
        len = 4095;
    m.data1 = (uint32_t)len;
    memcpy(m.payload, text, len);
    qk_sys_ipc_send(comp_pid, &m);
}
static int clip_get_fm(char *out, int max) {
    message_t m, r;
    memset(&m, 0, sizeof(m));
    m.type = MSG_CLIP_GET;
    qk_sys_ipc_send(comp_pid, &m);
    while (1) {
        qk_ipc_recv(&r);
        if (r.type == MSG_CLIP_DATA)
            break;
        if (stash_count < STASH_SIZE)
            stash_buf[stash_count++] = r;
    }
    int len = (int)r.data1;
    if (len > max - 1)
        len = max - 1;
    if (len > 0)
        memcpy(out, r.payload, len);
    out[len] = 0;
    return len;
}
#define PICKER_BAR_H 36
typedef struct {
    const char *label;
    const char *path;
    int kind;
    uint32_t icon_id;
    int disk_idx;
} sb_item_t;
#define SB_STATIC 10
#define SB_MAX_DISKS 8
#define SB_MAX (SB_STATIC + 1 + SB_MAX_DISKS)
static sb_item_t sb[SB_MAX] = {
    {"Home", "/USR/DEFAULT", 0, ICON_HOME, -1},
    {"Programs", "/PRG", 0, ICON_TERMINAL, -1},
    {"System", "/SYS", 0, ICON_MONITOR, -1},
    {"Users", "/USR", 0, ICON_USERS, -1},
    {"Temp", "/TMP", 0, ICON_CLOCK, -1},
    {0, 0, 1, 0, -1},
    {"Documents", "/USR/DEFAULT/DOCS", 0, ICON_FILE_TEXT, -1},
    {"Desktop", "/USR/DEFAULT/DESK", 0, ICON_FOLDER_OPEN, -1},
    {0, 0, 1, 0, -1},
    {"Root", "/", 0, ICON_HARD_DRIVE, -1},
};
static int sb_count = SB_STATIC;
static char disk_labels[SB_MAX_DISKS][24];
static int disk_ejected[SB_MAX_DISKS];
#define ICON_EJECT 0xEAF8u
static void rebuild_path(void);
static void load_dir(void);
static const char *disk_type_str(uint32_t type) {
    switch (type) {
    case 1:
        return "HDD";
    case 2:
        return "CD-ROM";
    case 3:
        return "Floppy";
    case 4:
        return "USB";
    default:
        return "Disk";
    }
}
static void init_disk_sidebar(void) {
    int ndisks = qk_disk_count();
    if (ndisks <= 0)
        return;
    if (ndisks > SB_MAX_DISKS)
        ndisks = SB_MAX_DISKS;
    memset(disk_ejected, 0, sizeof(disk_ejected));

    sb[sb_count].label = 0;
    sb[sb_count].path = 0;
    sb[sb_count].kind = 1;
    sb[sb_count].icon_id = 0;
    sb[sb_count].disk_idx = -1;
    sb_count++;
    for (int i = 0; i < ndisks && sb_count < SB_MAX; i++) {
        disk_info_t info;
        if (qk_disk_info(i, &info) != 0)
            continue;
        uint32_t mb = info.capacity_lo / 2048;
        const char *ts = disk_type_str(info.type);

        int p = 0;
        disk_labels[i][p++] = 's';
        disk_labels[i][p++] = 'd';
        disk_labels[i][p++] = '0' + (char)i;
        disk_labels[i][p++] = ' ';
        for (int j = 0; ts[j] && p < 16; j++)
            disk_labels[i][p++] = ts[j];
        disk_labels[i][p++] = ' ';
        disk_labels[i][p++] = '(';
        uint32_t val = mb;
        const char *unit = "MB";
        if (val >= 1024) {
            val = val / 1024;
            unit = "GB";
        }
        char num[12];
        int ni = 0;
        if (val == 0) {
            num[ni++] = '0';
        } else {
            uint32_t t = val;
            while (t) {
                num[ni++] = '0' + (t % 10);
                t /= 10;
            }
            for (int a = 0; a < ni / 2; a++) {
                char c = num[a];
                num[a] = num[ni - 1 - a];
                num[ni - 1 - a] = c;
            }
        }
        for (int j = 0; j < ni && p < 20; j++)
            disk_labels[i][p++] = num[j];
        disk_labels[i][p++] = ' ';
        for (int j = 0; unit[j]; j++)
            disk_labels[i][p++] = unit[j];
        disk_labels[i][p++] = ')';
        disk_labels[i][p] = 0;
        sb[sb_count].label = disk_labels[i];
        sb[sb_count].path = 0;
        sb[sb_count].kind = 2;
        sb[sb_count].icon_id = ICON_HARD_DRIVE;
        sb[sb_count].disk_idx = i;
        sb_count++;
    }
}
static void eject_disk(int disk_idx) {
    if (disk_idx <= 0 || disk_idx >= SB_MAX_DISKS)
        return;
    message_t m;
    memset(&m, 0, sizeof(m));
    m.type = MSG_DISK_EJECT;
    m.data1 = (uint32_t)disk_idx;
    qk_sys_ipc_send(storage_pid, &m);
    message_t r;
    qk_ipc_recv(&r);
    disk_ejected[disk_idx] = 1;
    if (fm_disk_idx == disk_idx) {
        fm_disk_idx = 0;
        dir_depth = 0;
        cur_cluster = 0;
        rebuild_path();
        load_dir();
    }
}
static void mount_disk(int disk_idx) {
    if (disk_idx < 0 || disk_idx >= SB_MAX_DISKS)
        return;
    message_t m;
    memset(&m, 0, sizeof(m));
    m.type = MSG_DISK_MOUNT;
    m.data1 = (uint32_t)disk_idx;
    qk_sys_ipc_send(storage_pid, &m);
    message_t r;
    qk_ipc_recv(&r);
    disk_ejected[disk_idx] = 0;
}
static void fill(int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0)
        return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > gfx_w)
        w = gfx_w - x;
    if (y + h > gfx_h)
        h = gfx_h - y;
    if (w <= 0 || h <= 0)
        return;
    for (int row = 0; row < h; row++) {
        uint32_t *p = gfx + (y + row) * gfx_w + x;
        for (int k = 0; k < w; k++)
            p[k] = c;
    }
}
static void fill_round(int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0)
        return;
    fill(x + 2, y, w - 4, h, c);
    fill(x, y + 2, 2, h - 4, c);
    fill(x + w - 2, y + 2, 2, h - 4, c);
    fill(x + 1, y + 1, 1, 1, c);
    fill(x + w - 2, y + 1, 1, 1, c);
    fill(x + 1, y + h - 2, 1, 1, c);
    fill(x + w - 2, y + h - 2, 1, 1, c);
}
static void dstr(int x, int y, const char *s, uint32_t c) {
    gfx_context_t ctx = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
    draw_string_vector(&ctx, x, y + 15, s, c);
}
static int str_w(const char *s) {
    int w = 0;
    while (*s) {
        w += get_char_width(*s);
        s++;
    }
    return w;
}
static bool str_ends_ext(const char *n, const char *ext3) {
    int l = 0;
    while (n[l])
        l++;
    if (l < 4 || n[l - 4] != '.')
        return false;
    for (int i = 0; i < 3; i++) {
        char c = n[l - 3 + i];
        if (c >= 'a' && c <= 'z')
            c -= 32;
        if (c != ext3[i])
            return false;
    }
    return true;
}
static int str_contains_ci(const char *hay, const char *needle) {
    if (!needle[0])
        return 1;
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        for (; needle[j] && hay[i + j]; j++) {
            char ch = hay[i + j], cn = needle[j];
            if (ch >= 'a' && ch <= 'z')
                ch -= 32;
            if (cn >= 'a' && cn <= 'z')
                cn -= 32;
            if (ch != cn)
                break;
        }
        if (!needle[j])
            return 1;
    }
    return 0;
}
static int str_eq_ci(const char *a, const char *b) {
    for (;;) {
        char ca = *a++, cb = *b++;
        if (ca >= 'a' && ca <= 'z')
            ca -= 32;
        if (cb >= 'a' && cb <= 'z')
            cb -= 32;
        if (ca != cb)
            return 0;
        if (!ca)
            return 1;
    }
}
static void draw_folder_icon(int x, int y, int w, int h) {
    int tab_h = h / 6;
    int tab_w = (w * 50) / 100;
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
static void draw_file_icon(int x, int y, int w, int h, uint32_t bg, uint32_t fold) {
    int fc = w / 5;
    fill(x, y, w - fc, h, bg);
    fill(x + w - fc, y + fc, fc, h - fc, bg);
    fill(x + w - fc, y, fc, fc, CB_CONTENT);
    fill(x + w - fc, y, 1, fc, fold);
    fill(x + w - fc, y + fc - 1, fc, 1, fold);
}
static void rebuild_path(void) {
    if (dir_depth == 0) {
        cur_path[0] = '/';
        cur_path[1] = 0;
        return;
    }
    int pos = 0;
    cur_path[0] = 0;
    for (int i = 1; i <= dir_depth; i++) {
        if (pos < 126)
            cur_path[pos++] = '/';
        for (int j = 0; dir_names[i][j] && pos < 126; j++)
            cur_path[pos++] = dir_names[i][j];
    }
    cur_path[pos] = 0;
}
static void load_dir(void) {
    entry_count = 0;
    selected = -1;
    grid_scroll = 0;
    search_active = 0;
    search_buf[0] = 0;
    search_len = 0;
    stash_count = 0;
    message_t req;
    memset(&req, 0, sizeof(req));
    req.type = MSG_DIR_REQ;
    req.data1 = cur_cluster;
    req.data3 = (uint32_t)fm_disk_idx;
    qk_sys_ipc_send(storage_pid, &req);
    while (1) {
        message_t resp;
        qk_ipc_recv(&resp);
        if (resp.sender_pid != storage_pid || resp.type != MSG_DIR_REPLY) {
            if (stash_count < STASH_SIZE)
                stash_buf[stash_count++] = resp;
            continue;
        }
        char *ptr = (char *)resp.payload;
        while (*ptr && entry_count < MAX_ENTRIES) {
            fm_entry_t e;
            e.is_dir = (*ptr == '1') ? 1 : 0;
            while (*ptr && *ptr != '|')
                ptr++;
            if (*ptr == '|')
                ptr++;
            int cl = 0;
            while (*ptr >= '0' && *ptr <= '9') {
                cl = cl * 10 + (*ptr - '0');
                ptr++;
            }
            e.cluster = (uint32_t)cl;
            if (*ptr == '|')
                ptr++;
            int ni = 0;
            while (*ptr && *ptr != '\n' && ni < 63)
                e.name[ni++] = *ptr++;
            e.name[ni] = 0;
            if (*ptr == '\n')
                ptr++;
            if (ni > 0 && !(e.name[0] == '.' && (ni == 1 || (ni == 2 && e.name[1] == '.'))))
                entries[entry_count++] = e;
        }
        if (resp.data1 == 0)
            break;
    }
}
static int navigate_to_path(const char *path) {
    if (!path || path[0] != '/')
        return 0;
    dir_depth = 0;
    cur_cluster = 0;
    if (path[1] == 0) {
        rebuild_path();
        return 1;
    }
    const char *p = path + 1;
    while (*p) {
        char seg[16];
        int si = 0;
        while (*p && *p != '/' && si < 15)
            seg[si++] = *p++;
        seg[si] = 0;
        if (*p == '/')
            p++;
        if (si == 0)
            continue;
        message_t req;
        memset(&req, 0, sizeof(req));
        req.type = MSG_DIR_REQ;
        req.data1 = cur_cluster;
        req.data3 = (uint32_t)fm_disk_idx;
        qk_sys_ipc_send(storage_pid, &req);
        int found = 0;
        uint16_t fc = 0;
        while (1) {
            message_t resp;
            qk_ipc_recv(&resp);
            if (resp.sender_pid != storage_pid || resp.type != MSG_DIR_REPLY) {
                if (stash_count < STASH_SIZE)
                    stash_buf[stash_count++] = resp;
                continue;
            }
            char *rp = (char *)resp.payload;
            while (*rp && !found) {
                int is_d = (*rp == '1');
                while (*rp && *rp != '|')
                    rp++;
                if (*rp == '|')
                    rp++;
                int cl = 0;
                while (*rp >= '0' && *rp <= '9') {
                    cl = cl * 10 + (*rp - '0');
                    rp++;
                }
                if (*rp == '|')
                    rp++;
                char nm[16];
                int ni = 0;
                while (*rp && *rp != '\n' && ni < 63)
                    nm[ni++] = *rp++;
                nm[ni] = 0;
                if (*rp == '\n')
                    rp++;
                if (is_d && ni > 0 && str_eq_ci(seg, nm)) {
                    found = 1;
                    fc = (uint32_t)cl;
                }
            }
            if (resp.data1 == 0)
                break;
        }
        if (!found)
            return 0;
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
static void build_search_cache(void) {
    search_cache_count = 0;

    static uint32_t q_cls[64];
    static char q_path[64][96];
    int qh = 0, qt = 0;
    q_cls[0] = 0;
    q_path[0][0] = '/';
    q_path[0][1] = 0;
    qt = 1;

    while (qh != qt && search_cache_count < MAX_ENTRIES) {
        uint16_t cls = q_cls[qh];
        char dir_path[96];
        strncpy(dir_path, q_path[qh], 95);
        dir_path[95] = 0;
        qh = (qh + 1) % 64;

        message_t req;
        memset(&req, 0, sizeof(req));
        req.type = MSG_DIR_REQ;
        req.data1 = cls;
        req.data3 = (uint32_t)fm_disk_idx;
        qk_sys_ipc_send(storage_pid, &req);

        while (1) {
            message_t resp;
            qk_ipc_recv(&resp);
            if (resp.sender_pid != storage_pid || resp.type != MSG_DIR_REPLY) {
                if (stash_count < STASH_SIZE)
                    stash_buf[stash_count++] = resp;
                continue;
            }
            char *ptr = (char *)resp.payload;
            while (*ptr && search_cache_count < MAX_ENTRIES) {
                fm_entry_t e;
                e.is_dir = (*ptr == '1') ? 1 : 0;
                while (*ptr && *ptr != '|')
                    ptr++;
                if (*ptr == '|')
                    ptr++;
                int cl = 0;
                while (*ptr >= '0' && *ptr <= '9') {
                    cl = cl * 10 + (*ptr - '0');
                    ptr++;
                }
                e.cluster = (uint32_t)cl;
                if (*ptr == '|')
                    ptr++;
                int ni = 0;
                while (*ptr && *ptr != '\n' && ni < 63)
                    e.name[ni++] = *ptr++;
                e.name[ni] = 0;
                if (*ptr == '\n')
                    ptr++;
                if (ni == 0 || (e.name[0] == '.' && (ni == 1 || (ni == 2 && e.name[1] == '.'))))
                    continue;

                search_cache[search_cache_count] = e;
                int dl = 0;
                while (dir_path[dl] && dl < 94) {
                    search_cache_paths[search_cache_count][dl] = dir_path[dl];
                    dl++;
                }
                if (dl > 1 && search_cache_paths[search_cache_count][dl - 1] != '/')
                    search_cache_paths[search_cache_count][dl++] = '/';
                for (int k = 0; e.name[k] && dl < 95; k++)
                    search_cache_paths[search_cache_count][dl++] = e.name[k];
                search_cache_paths[search_cache_count][dl] = 0;
                search_cache_count++;

                if (e.is_dir) {
                    int nq = (qt + 1) % 64;
                    if (nq != qh) {
                        q_cls[qt] = e.cluster;
                        int dl2 = 0;
                        while (dir_path[dl2] && dl2 < 93) {
                            q_path[qt][dl2] = dir_path[dl2];
                            dl2++;
                        }
                        if (dl2 > 1 && q_path[qt][dl2 - 1] != '/')
                            q_path[qt][dl2++] = '/';
                        for (int k = 0; e.name[k] && dl2 < 95; k++)
                            q_path[qt][dl2++] = e.name[k];
                        q_path[qt][dl2] = 0;
                        qt = nq;
                    }
                }
            }
            if (resp.data1 == 0)
                break;
        }
    }
}

static void do_global_search(void) {
    entry_count = 0;
    selected = -1;
    grid_scroll = 0;
    if (!search_buf[0])
        return;
    for (int i = 0; i < search_cache_count && entry_count < MAX_ENTRIES; i++) {
        if (str_contains_ci(search_cache[i].name, search_buf)) {
            entries[entry_count] = search_cache[i];
            strncpy(entry_paths[entry_count], search_cache_paths[i], 95);
            entry_paths[entry_count][95] = 0;
            entry_count++;
        }
    }
    selected = (entry_count > 0) ? 0 : -1;
}
static char sc_to_char(uint8_t sc) {
    static const char map[] = {
        0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', 0,   0,   0,   'q', 'w',
        'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 0,   0,   0,   0,   'a', 's', 'd', 'f', 'g', 'h',
        'j', 'k', 'l', 0,   0,   0,   0,   0,   'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    };
    if (sc < (uint8_t)sizeof(map))
        return map[sc];
    return 0;
}
static char sc_to_upper(uint8_t sc) {
    char c = sc_to_char(sc);
    if (c >= 'a' && c <= 'z')
        c -= 32;
    return c;
}
static int pfn_x_to_col(int mx) {
    int tx = SIDEBAR_W + 10 + 8;
    for (int i = 0; i < picker_fn_len; i++) {
        int cw = get_char_width(picker_filename[i]);
        if (mx < tx + cw / 2)
            return i;
        tx += cw;
    }
    return picker_fn_len;
}
static void render(void) {
    if (!gfx)
        return;
    fill(0, 0, gfx_w, gfx_h, CB_CONTENT);
    if (!user_font) {
        qk_win_dirty(comp_pid);
        return;
    }
    int fh = get_font_height();
    if (fh <= 0) {
        qk_win_dirty(comp_pid);
        return;
    }
    int content_w = gfx_w - SIDEBAR_W;
    int content_h = gfx_h - HEADER_H - (picker_mode ? PICKER_BAR_H : 0);
    int item_w = GITEM_W;
    fill(0, 0, gfx_w, HEADER_H, CB_HEADER);
    fill(0, HEADER_H - 1, gfx_w, 1, C_SEP);
    {
        int tw = str_w("Files");
        dstr((SIDEBAR_W - tw) / 2, (HEADER_H - fh) / 2, "Files", C_TEXT);
        if (gui_icons) {
            gfx_context_t ctx2 = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
            draw_icon_sized(&ctx2, gui_icons, 8, (HEADER_H - 16) / 2, ICON_SEARCH,
                            search_active ? C_ACCENT : C_DIM, 16);
        }
    }
    fill(SIDEBAR_W - 1, 0, 1, HEADER_H, C_SEP);
    {
        int icon_y = (HEADER_H - 16) / 2;
        gfx_context_t ctx2 = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
        if (gui_icons) {
            draw_icon_sized(&ctx2, gui_icons, CONTENT_X + 8, icon_y, ICON_ARROW_LEFT,
                            dir_depth > 0 ? C_TEXT : C_DIM, 16);
            draw_icon_sized(&ctx2, gui_icons, CONTENT_X + 30, icon_y, ICON_ARROW_RIGHT, C_DIM, 16);
            draw_icon_sized(&ctx2, gui_icons, CONTENT_X + 52, icon_y, ICON_REFRESH, C_TEXT, 16);
        } else {
            dstr(CONTENT_X + 10, (HEADER_H - fh) / 2, "<", dir_depth > 0 ? C_TEXT : C_DIM);
            dstr(CONTENT_X + 26, (HEADER_H - fh) / 2, ">", C_DIM);
            dstr(CONTENT_X + 48, (HEADER_H - fh) / 2, "R", C_TEXT);
        }
    }
    {
        int pill_x = CONTENT_X + 76;
        int pill_y = 5;
        int pill_w = content_w - 84;
        int pill_h = HEADER_H - 10;
        fill_round(pill_x, pill_y, pill_w, pill_h, path_editing ? CB_SEL : CB_SIDEBAR);
        int pill_ty = pill_y + (pill_h - fh) / 2;
        if (path_editing) {
            dstr(pill_x + 10, pill_ty, edit_buf, C_TEXT);
            int cx2 = pill_x + 10 + str_w(edit_buf);
            fill(cx2, pill_ty, 1, fh, C_TEXT);
        } else {
            const char *disp = (cur_path[0] == '/' && cur_path[1] == 0) ? "Root" : cur_path;
            dstr(pill_x + 10, pill_ty, disp, C_TEXT);
        }
    }
    fill(0, HEADER_H, SIDEBAR_W, gfx_h - HEADER_H, CB_SIDEBAR);
    fill(SIDEBAR_W - 1, HEADER_H, 1, gfx_h - HEADER_H, C_SEP);
#define SB_SEARCH_H 26
    if (search_active) {
        fill(0, HEADER_H, SIDEBAR_W - 1, SB_SEARCH_H, CB_SEL);
        fill(0, HEADER_H + SB_SEARCH_H - 1, SIDEBAR_W - 1, 1, C_SEP);
        int sty = HEADER_H + (SB_SEARCH_H - fh) / 2;
        if (search_buf[0]) {
            dstr(8, sty, search_buf, C_TEXT);
            fill(8 + str_w(search_buf), sty, 1, fh, C_TEXT);
        } else {
            dstr(8, sty, "Search...", C_DIM);
            fill(8, sty, 1, fh, C_TEXT);
        }
    }
    int sb_extra = search_active ? SB_SEARCH_H : 0;
    int active_sb = -1;
    for (int i = 0; i < sb_count; i++) {
        if (sb[i].kind == 2 && sb[i].disk_idx == fm_disk_idx && dir_depth == 0 &&
            cur_cluster == 0) {
            active_sb = i;
            break;
        }
        if (sb[i].kind == 0 && sb[i].path && fm_disk_idx == 0 && str_eq_ci(cur_path, sb[i].path)) {
            active_sb = i;
            break;
        }
    }
    {
        int sy = HEADER_H + 8 + sb_extra;
        for (int i = 0; i < sb_count; i++) {
            if (sb[i].kind == 1) {
                fill(12, sy + 4, SIDEBAR_W - 24, 1, C_SEP);
                sy += 12;
                continue;
            }
            if (!sb[i].label)
                continue;

            int ejected = (sb[i].kind == 2 && sb[i].disk_idx >= 0 &&
                           sb[i].disk_idx < SB_MAX_DISKS && disk_ejected[sb[i].disk_idx]);
            if (ejected) {
                continue;
            }
            if (i == active_sb)
                fill_round(6, sy + 2, SIDEBAR_W - 14, SB_ITEM_H - 4, CB_SEL);
            else if (i == hover_sb)
                fill_round(6, sy + 2, SIDEBAR_W - 14, SB_ITEM_H - 4, CB_HOVER);
            uint32_t col = i == active_sb ? C_TEXT : ejected ? 0xFF444444u : C_DIM;
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
    int dc = entry_count;
    if (dc == 0) {
        const char *msg = search_active ? (search_buf[0] ? "No results" : "Type to search...")
                                        : "Empty directory";
        int tw = str_w(msg);
        dstr(CONTENT_X + (content_w - tw) / 2, CONTENT_Y + (content_h - fh) / 2, msg, C_DIM);
        qk_win_dirty(comp_pid);
        return;
    }
    int rows_total = (dc + GCOLS - 1) / GCOLS;
    int rows_vis = content_h / GITEM_H;
    if (rows_vis < 1)
        rows_vis = 1;
    int max_scroll = rows_total - rows_vis;
    if (max_scroll < 0)
        max_scroll = 0;
    if (grid_scroll > max_scroll)
        grid_scroll = max_scroll;
    if (grid_scroll < 0)
        grid_scroll = 0;
    int icon_draw = 48;
    for (int row = 0; row <= rows_vis; row++) {
        int real_row = row + grid_scroll;
        if (real_row >= rows_total)
            break;
        for (int col = 0; col < GCOLS; col++) {
            int idx = real_row * GCOLS + col;
            if (idx >= dc)
                break;
            fm_entry_t *e = &entries[idx];
            int cx = CONTENT_X + col * item_w;
            int cy = CONTENT_Y + row * GITEM_H;
            if (cy >= gfx_h)
                continue;
            int visible_h = gfx_h - cy;
            if (visible_h <= 0)
                continue;
            if (idx == selected || idx == hover_grid) {
                int bx = cx + 10, by = cy + 6;
                int bw = item_w - 20;
                int bh = GITEM_H - 12;
                if (bh > visible_h - 4)
                    bh = visible_h - 4;
                fill_round(bx, by, bw, bh, idx == selected ? CB_SEL : CB_HOVER);
            }
            if (gui_icons) {
#define GICON_SZ 40
                int ix = cx + (item_w - GICON_SZ) / 2;
                int iy = cy + 20;
                gfx_context_t ctx2 = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
                int is_img = str_ends_ext(e->name, "BMP") || str_ends_ext(e->name, "PNG") ||
                             str_ends_ext(e->name, "JPG");
                uint32_t icon_id = e->is_dir ? ICON_FOLDER
                                   : str_ends_ext(e->name, "QEX") || str_ends_ext(e->name, "ELF")
                                       ? ICON_TERMINAL
                                   : str_ends_ext(e->name, "BIN") ? ICON_BINARY
                                   : is_img                       ? ICON_IMAGE
                                                                  : ICON_FILE_TEXT;
                uint32_t ic = e->is_dir ? C_ACCENT
                              : str_ends_ext(e->name, "QEX") || str_ends_ext(e->name, "ELF")
                                  ? 0xFF80CC80u
                              : str_ends_ext(e->name, "BIN") ? 0xFFCC9944u
                              : is_img                       ? 0xFF88AADDu
                                                             : C_TEXT;
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
                strncpy(label, e->name, 15);
                label[15] = 0;
                int tw = str_w(label);
                while (tw > item_w - 8 && label[0]) {
                    int l = 0;
                    while (label[l])
                        l++;
                    if (l <= 1)
                        break;
                    label[l - 1] = 0;
                    tw = str_w(label);
                }
                int tx = cx + (item_w - tw) / 2;
                dstr(tx, ty, label, C_TEXT);
            }
        }
    }
    if (rows_total > rows_vis) {
        int sx = gfx_w - SCROLLBAR_W;
        int sy = CONTENT_Y;
        int sh = content_h;
        fill(sx, sy, SCROLLBAR_W, sh, CB_SIDEBAR);
        int th = sh * rows_vis / rows_total;
        if (th < 16)
            th = 16;
        int ty2 = sy;
        if (rows_total > rows_vis)
            ty2 = sy + (sh - th) * grid_scroll / (rows_total - rows_vis);
        fill_round(sx + 1, ty2, SCROLLBAR_W - 2, th, C_DIM);
    }

    if (picker_mode) {
        int bar_y = gfx_h - PICKER_BAR_H;
        fill(SIDEBAR_W, bar_y, gfx_w - SIDEBAR_W, PICKER_BAR_H, CB_HEADER);
        fill(SIDEBAR_W, bar_y, gfx_w - SIDEBAR_W, 1, C_SEP);

        int cancel_x = gfx_w - 170;
        int action_x = gfx_w - 90;
        const char *action_label = (picker_mode == 2) ? "Save" : "Open";

        fill_round(cancel_x, bar_y + 6, 70, 24, CB_SEL);
        int cw = str_w("Cancel");
        dstr(cancel_x + (70 - cw) / 2, bar_y + 10, "Cancel", C_TEXT);

        fill_round(action_x, bar_y + 6, 70, 24, C_ACCENT);
        int aw = str_w(action_label);
        dstr(action_x + (70 - aw) / 2, bar_y + 10, action_label, 0xFF1A1A1Au);

        if (picker_mode == 2) {
            int field_x = SIDEBAR_W + 10;
            int field_w = cancel_x - field_x - 10;
            fill_round(field_x, bar_y + 6, field_w, 24, CB_SIDEBAR);
            if (picker_filename[0]) {
                gfx_context_t ctx2 = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
                int tx = field_x + 8;
                int ps0 = -1, ps1 = -1;
                if (pfn_sel_start >= 0 && pfn_sel_start != pfn_sel_end) {
                    ps0 = pfn_sel_start < pfn_sel_end ? pfn_sel_start : pfn_sel_end;
                    ps1 = pfn_sel_start < pfn_sel_end ? pfn_sel_end : pfn_sel_start;
                }
                for (int ci = 0; ci < picker_fn_len; ci++) {
                    int cw = get_char_width(picker_filename[ci]);
                    if (ps0 >= 0 && ci >= ps0 && ci < ps1)
                        fill(tx, bar_y + 8, cw, 20, 0xFF264F78u);
                    tx = draw_codepoint_vector(&ctx2, tx, bar_y + 25, (uint8_t)picker_filename[ci],
                                               C_TEXT);
                }
                if (picker_fn_editing && ps0 < 0) {
                    int cx2 = field_x + 8;
                    for (int ci = 0; ci < pfn_cursor && ci < picker_fn_len; ci++)
                        cx2 += get_char_width(picker_filename[ci]);
                    fill(cx2, bar_y + 8, 1, 20, C_TEXT);
                }
            } else {
                dstr(field_x + 8, bar_y + 10, "Filename...", C_DIM);
                if (picker_fn_editing)
                    fill(field_x + 8, bar_y + 8, 1, 20, C_TEXT);
            }
        }
    }

    qk_win_dirty(comp_pid);
}
static int spawn_qex(const char *qex_path) {
    message_t req, resp;
    memset(&req, 0, sizeof(req));
    req.type = MSG_FILE_OPEN;
    req.data1 = 0xFFFF;
    strncpy((char *)req.payload, qex_path, sizeof(req.payload) - 1);
    qk_ipc_send_recv(storage_pid, &req, &resp);
    if (resp.type != MSG_FILE_SIZE || resp.data1 == 0)
        return -1;
    uint32_t size = resp.data1;
    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf)
        return -1;
    uint32_t off = 0;
    while (off < size) {
        memset(&req, 0, sizeof(req));
        req.type = MSG_REQ_DATA;
        req.data1 = off;
        req.data2 = sizeof(resp.payload);
        qk_ipc_send_recv(storage_pid, &req, &resp);
        if (resp.type != MSG_FILE_DATA)
            break;
        uint32_t got = resp.data1;
        if (got == 0)
            break;
        if (off + got > size)
            got = size - off;
        memcpy(buf + off, resp.payload, got);
        off += got;
    }
    int pid = (off > 0) ? qk_spawn(buf, size) : -1;
    free(buf);
    return pid;
}
static void open_with(const char *app_qex, const char *file_path) {
    int pid = spawn_qex(app_qex);
    if (pid < 0)
        return;
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_FILE_OPEN;
    msg.data3 = (uint32_t)fm_disk_idx;
    strncpy((char *)msg.payload, file_path, sizeof(msg.payload) - 1);
    qk_sys_ipc_send(pid, &msg);
}

static void picker_build_path(char *out, int outsz, const char *name) {
    strncpy(out, cur_path, outsz - 20);
    int plen = strlen(out);
    if (plen > 0 && out[plen - 1] != '/')
        out[plen++] = '/';
    strncpy(out + plen, name, outsz - plen - 1);
    out[outsz - 1] = 0;
}
static void picker_send_result(const char *path) {
    message_t r;
    memset(&r, 0, sizeof(r));
    r.type = MSG_PICKER_RESULT;
    r.data1 = 1;
    if (path)
        strncpy((char *)r.payload, path, sizeof(r.payload) - 1);
    qk_sys_ipc_send(requester_pid, &r);
    qk_sys_exit(0);
}
static void picker_send_cancel(void) {
    message_t r;
    memset(&r, 0, sizeof(r));
    r.type = MSG_PICKER_RESULT;
    r.data1 = 0;
    qk_sys_ipc_send(requester_pid, &r);
    qk_sys_exit(0);
}

static void launch_entry(int idx) {
    if (idx < 0 || idx >= entry_count)
        return;
    fm_entry_t *e = &entries[idx];

    if (picker_mode && !e->is_dir) {
        char fp[128];
        if (search_active)
            strncpy(fp, entry_paths[idx], 127);
        else
            picker_build_path(fp, sizeof(fp), e->name);
        fp[127] = 0;
        if (picker_mode == 1) {
            picker_send_result(fp);
            return;
        } else {
            strncpy(picker_filename, e->name, 63);
            picker_filename[63] = 0;
            picker_fn_len = strlen(picker_filename);
            pfn_cursor = picker_fn_len;
            pfn_sel_start = pfn_sel_end = -1;
            picker_fn_editing = 1;
            selected = idx;
            render();
            return;
        }
    }

    if (search_active) {
        const char *fp = entry_paths[idx];
        if (e->is_dir) {
            if (navigate_to_path(fp)) {
                search_active = 0;
                search_buf[0] = 0;
                search_len = 0;
                load_dir();
                render();
            }
            return;
        }
        if (str_ends_ext(e->name, "BMP")) {
            open_with("/PRG/IMGVIEW.QEX", fp);
            return;
        }
        if (str_ends_ext(e->name, "TXT")) {
            open_with("/PRG/NOTEPAD.QEX", fp);
            return;
        }
        if (str_ends_ext(e->name, "QEX")) {
            spawn_qex(fp);
            return;
        }
        if (str_ends_ext(e->name, "TTF") || str_ends_ext(e->name, "BIN"))
            return;
        return;
    }
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
    if (plen > 0 && full_path[plen - 1] != '/')
        full_path[plen++] = '/';
    strncpy(full_path + plen, e->name, sizeof(full_path) - plen - 1);
    full_path[sizeof(full_path) - 1] = 0;
    if (str_ends_ext(e->name, "BMP")) {
        open_with("/PRG/IMGVIEW.QEX", full_path);
        return;
    }
    if (str_ends_ext(e->name, "TXT")) {
        open_with("/PRG/NOTEPAD.QEX", full_path);
        return;
    }
    if (str_ends_ext(e->name, "QEX")) {
        spawn_qex(full_path);
        return;
    }
    if (str_ends_ext(e->name, "TTF") || str_ends_ext(e->name, "BIN"))
        return;
}
int main(void) {
    comp_pid = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    storage_pid = qk_get_server_pid(QK_SERVER_STORAGE);
    if (comp_pid < 0 || storage_pid < 0)
        return 1;
    init_disk_sidebar();

    {
        message_t sub;
        memset(&sub, 0, sizeof(sub));
        sub.type = MSG_DISK_SUBSCRIBE;
        qk_sys_ipc_send(storage_pid, &sub);
    }
    void *font_addr = 0;
    if (qk_map_font_atlas(&font_addr) == 0)
        font_atlas_init((uint8_t *)font_addr);
    void *icons_addr = 0;
    if (qk_map_icons_atlas(&icons_addr) == 0 && icons_addr) {
        gui_icons = (AtlasFont *)malloc(sizeof(AtlasFont));
        font_atlas_init_for_ptr(gui_icons, (uint8_t *)icons_addr);
    }

    {
        message_t probe;
        while (qk_ipc_recv_nb(&probe) == 0) {
            if (probe.type == MSG_PICKER_OPEN) {
                requester_pid = probe.sender_pid;
                picker_mode = (probe.data1 == 1) ? 2 : 1;
                if (probe.payload[0])
                    strncpy(picker_filename, (char *)probe.payload, 63);
                else
                    strncpy(picker_filename, "UNTITLED.TXT", 63);
                picker_filename[63] = 0;
                picker_fn_len = strlen(picker_filename);
                pfn_cursor = picker_fn_len;
                if (picker_mode == 2)
                    picker_fn_editing = 1;
                break;
            }
            if (stash_count < STASH_SIZE)
                stash_buf[stash_count++] = probe;
        }
    }
    const char *win_title = picker_mode == 1   ? "Open File"
                            : picker_mode == 2 ? "Save File"
                                               : "Files";
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_WIN_CREATE;
    msg.data1 = WIN_W;
    msg.data2 = WIN_H;
    strncpy((char *)msg.payload, win_title, 63);
    qk_sys_ipc_send(comp_pid, &msg);
    while (1) {
        qk_ipc_recv(&msg);
        if (msg.type == MSG_WIN_CREATED)
            break;
    }
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
        if (stash_count > 0) {
            msg = stash_buf[--stash_count];
        } else {
            qk_ipc_recv(&msg);
        }
        int needs_render = 0;
        do {
            if (msg.type == MSG_DISK_CHANGED) {
                int di = (int)msg.data1;
                int mounted = (int)msg.data2;
                if (di >= 0 && di < SB_MAX_DISKS) {
                    disk_ejected[di] = mounted ? 0 : 1;
                    if (!mounted && fm_disk_idx == di) {
                        fm_disk_idx = 0;
                        dir_depth = 0;
                        cur_cluster = 0;
                        rebuild_path();
                        load_dir();
                    }
                    needs_render = 1;
                }
            } else if (msg.type == MSG_WIN_RESIZED) {
                uint32_t *ng = (uint32_t *)qk_win_map_buffer(comp_pid, win_id, &gfx_w, &gfx_h);
                if (ng)
                    gfx = ng;
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
                    int sb_extra_h = search_active ? SB_SEARCH_H : 0;
                    if (mx >= 0 && mx < SIDEBAR_W && my >= HEADER_H) {
                        int sy2 = HEADER_H + 8 + sb_extra_h;
                        for (int i = 0; i < sb_count; i++) {
                            if (sb[i].kind == 1) {
                                sy2 += 12;
                                continue;
                            }
                            if (!sb[i].label)
                                continue;
                            if (sb[i].kind == 2 && sb[i].disk_idx > 0 &&
                                sb[i].disk_idx < SB_MAX_DISKS && disk_ejected[sb[i].disk_idx])
                                continue;
                            if (my >= sy2 && my < sy2 + SB_ITEM_H)
                                new_hsb = i;
                            sy2 += SB_ITEM_H;
                        }
                    }
                    if (mx >= CONTENT_X && mx < CONTENT_X + GCOLS * GITEM_W && my >= CONTENT_Y) {
                        int col2 = (mx - CONTENT_X) / GITEM_W;
                        int rows_vis2 = (gfx_h - HEADER_H) / GITEM_H;
                        int row2 = (my - CONTENT_Y) / GITEM_H;
                        if (row2 >= 0 && row2 <= rows_vis2) {
                            int idx2 = (row2 + grid_scroll) * GCOLS + col2;
                            if (idx2 >= 0 && idx2 < entry_count)
                                new_hg = idx2;
                        }
                    }
                    if (pfn_mouse_sel && picker_fn_editing) {
                        int col = pfn_x_to_col(mx);
                        pfn_sel_end = col;
                        pfn_cursor = col;
                        needs_render = 1;
                    }
                    if (new_hsb != hover_sb || new_hg != hover_grid)
                        needs_render = 1;
                    hover_sb = new_hsb;
                    hover_grid = new_hg;
                } else if (ev->type == EVENT_MOUSE_DOWN && ev->button == MOUSE_BTN_LEFT) {
                    int cx = ev->x;
                    int cy = ev->y - TITLE_BAR_HEIGHT;
                    if (cy >= 0 && cy < HEADER_H && cx >= CONTENT_X + 4 && cx < CONTENT_X + 38 &&
                        dir_depth > 0) {
                        dir_depth--;
                        cur_cluster = dir_stack[dir_depth];
                        rebuild_path();
                        load_dir();
                        needs_render = 1;
                    } else if (cy >= 0 && cy < HEADER_H && cx >= CONTENT_X + 48 &&
                               cx < CONTENT_X + 70) {
                        load_dir();
                        needs_render = 1;
                    } else if (cy >= 5 && cy < HEADER_H - 5 && cx >= CONTENT_X + 76 &&
                               cx < gfx_w - 8) {
                        if (!path_editing) {
                            path_editing = 1;
                            strncpy(edit_buf, cur_path, 127);
                            edit_buf[127] = 0;
                            edit_len = strlen(edit_buf);
                            needs_render = 1;
                        }
                    } else if (cx >= 0 && cx < SIDEBAR_W && cy >= 0 && cy < HEADER_H) {
                        search_active = !search_active;
                        if (!search_active) {
                            search_buf[0] = 0;
                            search_len = 0;
                            load_dir();
                        } else {
                            search_buf[0] = 0;
                            search_len = 0;
                            build_search_cache();
                            do_global_search();
                        }
                        needs_render = 1;
                    } else if (cx >= 0 && cx < SIDEBAR_W && cy >= HEADER_H) {
                        int sb_extra_c = search_active ? SB_SEARCH_H : 0;
                        int sy = HEADER_H + 8 + sb_extra_c;
                        for (int i = 0; i < sb_count; i++) {
                            if (sb[i].kind == 1) {
                                sy += 12;
                                continue;
                            }
                            if (!sb[i].label)
                                continue;
                            if (sb[i].kind == 2 && sb[i].disk_idx > 0 &&
                                sb[i].disk_idx < SB_MAX_DISKS && disk_ejected[sb[i].disk_idx])
                                continue;
                            if (cy >= sy && cy < sy + SB_ITEM_H) {
                                if (sb[i].kind == 2 && sb[i].disk_idx >= 0) {
                                    int di = sb[i].disk_idx;

                                    fm_disk_idx = di;
                                    dir_depth = 0;
                                    cur_cluster = 0;
                                    rebuild_path();
                                    load_dir();
                                    needs_render = 1;
                                } else if (sb[i].path) {
                                    int prev_disk = fm_disk_idx;
                                    fm_disk_idx = 0;
                                    if (navigate_to_path(sb[i].path)) {
                                        load_dir();
                                        needs_render = 1;
                                    } else {
                                        fm_disk_idx = prev_disk;
                                    }
                                }
                                break;
                            }
                            sy += SB_ITEM_H;
                        }
                    } else if (picker_mode && cy >= (int)(gfx_h - PICKER_BAR_H)) {
                        int bar_y = gfx_h - PICKER_BAR_H;
                        int cancel_x = gfx_w - 170;
                        int action_x = gfx_w - 90;
                        if (cx >= cancel_x && cx < cancel_x + 70 && cy >= bar_y + 6 &&
                            cy < bar_y + 30) {
                            picker_send_cancel();
                        } else if (cx >= action_x && cx < action_x + 70 && cy >= bar_y + 6 &&
                                   cy < bar_y + 30) {
                            if (picker_mode == 1 && selected >= 0 && !entries[selected].is_dir) {
                                char fp[128];
                                picker_build_path(fp, sizeof(fp), entries[selected].name);
                                picker_send_result(fp);
                            } else if (picker_mode == 2 && picker_filename[0]) {
                                char fp[128];
                                picker_build_path(fp, sizeof(fp), picker_filename);
                                picker_send_result(fp);
                            }
                        } else if (picker_mode == 2 && cx >= SIDEBAR_W + 10 && cx < cancel_x - 10) {
                            picker_fn_editing = 1;
                            int pcol = pfn_x_to_col(cx);
                            pfn_cursor = pcol;
                            pfn_sel_start = pcol;
                            pfn_sel_end = pcol;
                            pfn_mouse_sel = true;
                            needs_render = 1;
                        }
                    } else if (cx >= CONTENT_X && cx < CONTENT_X + GCOLS * GITEM_W &&
                               cy >= CONTENT_Y) {
                        int col = (cx - CONTENT_X) / GITEM_W;
                        int row_s = (cy - CONTENT_Y) / GITEM_H;
                        int idx = (row_s + grid_scroll) * GCOLS + col;
                        if (idx >= 0 && idx < entry_count) {
                            if (idx == selected) {
                                if (needs_render)
                                    render();
                                needs_render = 0;
                                launch_entry(idx);
                            } else {
                                selected = idx;
                                needs_render = 1;
                            }
                        }
                    }
                } else if (ev->type == EVENT_MOUSE_UP) {
                    pfn_mouse_sel = false;
                }
            } else if (msg.type == MSG_KEY_EVENT) {
                uint8_t sc = (uint8_t)(msg.data1 & 0x7F);
                bool released = (msg.data1 & 0x80) != 0;
                static bool lshift = false, rshift = false, lctrl = false;
                if (sc == 0x2A) {
                    lshift = !released;
                    continue;
                }
                if (sc == 0x36) {
                    rshift = !released;
                    continue;
                }
                if (sc == 0x1D) {
                    lctrl = !released;
                    continue;
                }
                int shifted = lshift || rshift;
                if (!released) {
                    if (picker_fn_editing) {
                        if (lctrl && sc == 0x1E) {
                            pfn_sel_start = 0;
                            pfn_sel_end = picker_fn_len;
                            pfn_cursor = picker_fn_len;
                            needs_render = 1;
                        } else if (lctrl && sc == 0x2E) {
                            if (pfn_sel_start >= 0 && pfn_sel_start != pfn_sel_end) {
                                int cs0 = pfn_sel_start < pfn_sel_end ? pfn_sel_start : pfn_sel_end;
                                int cs1 = pfn_sel_start < pfn_sel_end ? pfn_sel_end : pfn_sel_start;
                                clip_set_fm(picker_filename + cs0, cs1 - cs0);
                            }
                        } else if (lctrl && sc == 0x2D) {
                            if (pfn_sel_start >= 0 && pfn_sel_start != pfn_sel_end) {
                                int cs0 = pfn_sel_start < pfn_sel_end ? pfn_sel_start : pfn_sel_end;
                                int cs1 = pfn_sel_start < pfn_sel_end ? pfn_sel_end : pfn_sel_start;
                                clip_set_fm(picker_filename + cs0, cs1 - cs0);
                                memmove(picker_filename + cs0, picker_filename + cs1,
                                        picker_fn_len - cs1 + 1);
                                picker_fn_len -= (cs1 - cs0);
                                pfn_cursor = cs0;
                                pfn_sel_start = pfn_sel_end = -1;
                                needs_render = 1;
                            }
                        } else if (lctrl && sc == 0x2F) {
                            char cb[64];
                            int cl = clip_get_fm(cb, sizeof(cb));
                            if (cl > 0) {
                                if (pfn_sel_start >= 0 && pfn_sel_start != pfn_sel_end) {
                                    int cs0 =
                                        pfn_sel_start < pfn_sel_end ? pfn_sel_start : pfn_sel_end;
                                    int cs1 =
                                        pfn_sel_start < pfn_sel_end ? pfn_sel_end : pfn_sel_start;
                                    memmove(picker_filename + cs0, picker_filename + cs1,
                                            picker_fn_len - cs1 + 1);
                                    picker_fn_len -= (cs1 - cs0);
                                    pfn_cursor = cs0;
                                    pfn_sel_start = pfn_sel_end = -1;
                                }
                                if (picker_fn_len + cl < 63) {
                                    memmove(picker_filename + pfn_cursor + cl,
                                            picker_filename + pfn_cursor,
                                            picker_fn_len - pfn_cursor + 1);
                                    memcpy(picker_filename + pfn_cursor, cb, cl);
                                    pfn_cursor += cl;
                                    picker_fn_len += cl;
                                    needs_render = 1;
                                }
                            }
                        } else if (sc == 0x01) {
                            picker_fn_editing = 0;
                            pfn_sel_start = pfn_sel_end = -1;
                            needs_render = 1;
                        } else if (sc == 0x1C) {
                            if (picker_filename[0]) {
                                char fp[128];
                                picker_build_path(fp, sizeof(fp), picker_filename);
                                picker_send_result(fp);
                            }
                        } else if (sc == 0x0E) {
                            if (pfn_sel_start >= 0 && pfn_sel_start != pfn_sel_end) {
                                int ps0 = pfn_sel_start < pfn_sel_end ? pfn_sel_start : pfn_sel_end;
                                int ps1 = pfn_sel_start < pfn_sel_end ? pfn_sel_end : pfn_sel_start;
                                memmove(picker_filename + ps0, picker_filename + ps1,
                                        picker_fn_len - ps1 + 1);
                                picker_fn_len -= (ps1 - ps0);
                                pfn_cursor = ps0;
                                pfn_sel_start = pfn_sel_end = -1;
                                needs_render = 1;
                            } else if (pfn_cursor > 0) {
                                memmove(picker_filename + pfn_cursor - 1,
                                        picker_filename + pfn_cursor,
                                        picker_fn_len - pfn_cursor + 1);
                                pfn_cursor--;
                                picker_fn_len--;
                                needs_render = 1;
                            }
                        } else {
                            char ch = shifted ? sc_to_upper(sc) : sc_to_char(sc);
                            if (ch) {
                                if (pfn_sel_start >= 0 && pfn_sel_start != pfn_sel_end) {
                                    int ps0 =
                                        pfn_sel_start < pfn_sel_end ? pfn_sel_start : pfn_sel_end;
                                    int ps1 =
                                        pfn_sel_start < pfn_sel_end ? pfn_sel_end : pfn_sel_start;
                                    memmove(picker_filename + ps0, picker_filename + ps1,
                                            picker_fn_len - ps1 + 1);
                                    picker_fn_len -= (ps1 - ps0);
                                    pfn_cursor = ps0;
                                    pfn_sel_start = pfn_sel_end = -1;
                                }
                                if (picker_fn_len < 63) {
                                    memmove(picker_filename + pfn_cursor + 1,
                                            picker_filename + pfn_cursor,
                                            picker_fn_len - pfn_cursor + 1);
                                    picker_filename[pfn_cursor++] = ch;
                                    picker_fn_len++;
                                    needs_render = 1;
                                }
                            }
                        }
                    } else if (path_editing) {
                        if (sc == 0x01) {
                            path_editing = 0;
                            needs_render = 1;
                        } else if (sc == 0x1C) {
                            path_editing = 0;
                            if (navigate_to_path(edit_buf)) {
                                load_dir();
                            }
                            needs_render = 1;
                        } else if (sc == 0x0E) {
                            if (edit_len > 0) {
                                edit_buf[--edit_len] = 0;
                                needs_render = 1;
                            }
                        } else {
                            char ch = sc_to_char(sc);
                            if (ch && edit_len < 126) {
                                edit_buf[edit_len++] = ch;
                                edit_buf[edit_len] = 0;
                                needs_render = 1;
                            }
                        }
                    } else if (search_active) {
                        if (sc == 0x01) {
                            search_active = 0;
                            search_buf[0] = 0;
                            search_len = 0;
                            load_dir();
                            needs_render = 1;
                        } else if (sc == 0x0E) {
                            if (search_len > 0) {
                                search_buf[--search_len] = 0;
                                do_global_search();
                                needs_render = 1;
                            }
                        } else if (sc == 0x1C) {
                            if (selected >= 0 && selected < entry_count) {
                                if (needs_render)
                                    render();
                                needs_render = 0;
                                launch_entry(selected);
                            }
                        } else {
                            char ch = sc_to_char(sc);
                            if (ch && search_len < 63) {
                                search_buf[search_len++] = ch;
                                search_buf[search_len] = 0;
                                do_global_search();
                                needs_render = 1;
                            }
                        }
                    } else {
                        int rows_total_k = (entry_count + GCOLS - 1) / GCOLS;
                        int rows_vis_k = (gfx_h - HEADER_H) / GITEM_H;
                        int max_s = rows_total_k - rows_vis_k;
                        if (max_s < 0)
                            max_s = 0;
                        if (sc == 0x4B) {
                            if (selected > 0)
                                selected--;
                            needs_render = 1;
                        } else if (sc == 0x4D) {
                            if (selected < entry_count - 1)
                                selected++;
                            needs_render = 1;
                        } else if (sc == 0x48) {
                            if (selected >= GCOLS)
                                selected -= GCOLS;
                            needs_render = 1;
                        } else if (sc == 0x50) {
                            if (selected + GCOLS < entry_count)
                                selected += GCOLS;
                            needs_render = 1;
                        } else if (sc == 0x1C) {
                            if (selected >= 0) {
                                if (needs_render)
                                    render();
                                needs_render = 0;
                                launch_entry(selected);
                            }
                        } else if (sc == 0x0E) {
                            if (dir_depth > 0) {
                                dir_depth--;
                                cur_cluster = dir_stack[dir_depth];
                                rebuild_path();
                                load_dir();
                                needs_render = 1;
                            }
                        } else if (sc == 0x3F) {
                            load_dir();
                            needs_render = 1;
                        }
                        if (needs_render && selected >= 0) {
                            int sel_row = selected / GCOLS;
                            if (sel_row < grid_scroll)
                                grid_scroll = sel_row;
                            if (sel_row >= grid_scroll + rows_vis_k)
                                grid_scroll = sel_row - rows_vis_k + 1;
                            if (grid_scroll > max_s)
                                grid_scroll = max_s;
                            if (grid_scroll < 0)
                                grid_scroll = 0;
                        }
                    }
                }
            }
        } while (qk_ipc_recv_nb(&msg) == 0);
        if (needs_render)
            render();
    }
    return 0;
}
