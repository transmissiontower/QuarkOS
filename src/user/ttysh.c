/*
 * ttysh – windowless shell that runs inside a terminal emulator.
 *
 * Startup protocol:
 *   1. Wait for MSG_TERM_ATTACH  (data1 = terminal's PID)
 *   2. All output  → MSG_WIN_PRINT  to master_pid
 *   3. All input   → MSG_KEY_EVENT  forwarded by master_pid
 */
#include "kernel/ipc.h"
#include "lib/stdbool.h"
#include "lib/stdlib.h"
#include "user/lib/libapi.h"
#include "user/lib/path.h"
#include "user/lib/storage_client.h"
#include <stddef.h>

/* ANSI color shortcuts */
#define C_RESET  "\033[0m"
#define C_DIM    "\033[2m"
#define C_RED    "\033[31m"
#define C_GREEN  "\033[32m"
#define C_YELLOW "\033[33m"
#define C_CYAN   "\033[36m"
#define C_BCYAN  "\033[96m"

static int master_pid  = -1;
static int storage_pid = -1;

static uint16_t current_dir_cluster = 0;
static char     current_path[128]   = "/";

#define MAX_PATH_DEPTH   16
#define NANO_BUFFER_SIZE 4096
#define CMD_BUF_SIZE     256

#define SCANCODE_ARROW_UP   0x48
#define SCANCODE_ARROW_DOWN 0x50
#define SCANCODE_CTRL       0x1D
#define SPECIAL_CTRL_C      0x03

static uint16_t dir_stack[MAX_PATH_DEPTH] = {0};
static char     dir_names[MAX_PATH_DEPTH][16];
static int      dir_depth = 0;

typedef struct { int is_dir; uint16_t cluster; char name[16]; } dir_entry_info_t;

/* ── I/O ──────────────────────────────────────────────────────────────────── */

static void tty_print(const char *text)
{
    if (!text || master_pid < 0) return;
    const char *ptr = text;
    while (*ptr) {
        char chunk[120]; int idx = 0;
        while (*ptr && idx < (int)sizeof(chunk)-1) chunk[idx++] = *ptr++;
        chunk[idx] = 0;
        if (!idx) break;
        message_t msg;
        msg.type = MSG_WIN_PRINT;
        int len = idx < 127 ? idx : 127;
        strncpy((char *)msg.payload, chunk, len);
        msg.payload[len] = 0;
        qk_sys_ipc_send(master_pid, &msg);
    }
}

static void tty_clear(void)
{
    if (master_pid < 0) return;
    message_t msg; msg.type = MSG_WIN_CLEAR;
    qk_sys_ipc_send(master_pid, &msg);
}

/* ── string utils ─────────────────────────────────────────────────────────── */

static int my_strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char *)a - *(const unsigned char *)b;
}

static char upcase(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

static int name_eq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) { if (upcase(*a) != upcase(*b)) return 0; a++; b++; }
    return (*a == 0 && *b == 0);
}

/* ── keyboard ─────────────────────────────────────────────────────────────── */

#define KEY_MOD_SHIFT 0x01
#define KEY_MOD_CAPS  0x08

static char scancode_to_char(uint8_t code, uint32_t mods)
{
    bool sh  = (mods & KEY_MOD_SHIFT) != 0;
    bool cap = (mods & KEY_MOD_CAPS)  != 0;
    const char *r1  = "1234567890-=",  *r1s = "!@#$%^&*()_+";
    const char *r2  = "qwertyuiop[]",  *r2s = "QWERTYUIOP{}";
    const char *r3  = "asdfghjkl;'`",  *r3s = "ASDFGHJKL:\"~";
    const char *r4  = "zxcvbnm,./",    *r4s = "ZXCVBNM<>?";
    bool up = sh ^ cap;
    if (code >= 0x02 && code <= 0x0D) return sh  ? r1s[code-0x02] : r1[code-0x02];
    if (code >= 0x10 && code <= 0x1B) return up  ? r2s[code-0x10] : r2[code-0x10];
    if (code >= 0x1E && code <= 0x29) return (code <= 0x26 ? up : sh) ? r3s[code-0x1E] : r3[code-0x1E];
    if (code >= 0x2C && code <= 0x35) return (code <= 0x32 ? up : sh) ? r4s[code-0x2C] : r4[code-0x2C];
    if (code == 0x39) return ' ';
    if (code == 0x1C) return '\n';
    if (code == 0x0E) return '\b';
    if (code == 0x0F) return '\t';
    return 0;
}

/* ── path helpers ─────────────────────────────────────────────────────────── */

static void rebuild_path(void)
{
    if (dir_depth == 0) { current_path[0] = '/'; current_path[1] = 0; return; }
    int pos = 0;
    for (int i = 1; i <= dir_depth; i++) {
        if (pos < 126) current_path[pos++] = '/';
        for (int j = 0; dir_names[i][j] && pos < 126; j++)
            current_path[pos++] = dir_names[i][j];
    }
    current_path[pos] = 0;
}

static int dir_lookup(uint16_t clus, const char *target, dir_entry_info_t *out)
{
    if (!target || !*target) return -1;
    message_t req; memset(&req, 0, sizeof(req));
    req.type = MSG_DIR_REQ; req.data1 = clus;
    qk_sys_ipc_send(storage_pid, &req);
    int found = 0; dir_entry_info_t res;
    while (1) {
        message_t resp; qk_ipc_recv(&resp);
        if (resp.sender_pid != storage_pid || resp.type != MSG_DIR_REPLY) continue;
        char *ptr = (char *)resp.payload;
        while (*ptr) {
            dir_entry_info_t e;
            e.is_dir = (*ptr == '1') ? 1 : 0;
            while (*ptr && *ptr != '|') ptr++; if (*ptr == '|') ptr++;
            int cl = 0; while (*ptr >= '0' && *ptr <= '9') cl = cl*10 + (*ptr++ - '0');
            e.cluster = (uint16_t)cl; if (*ptr == '|') ptr++;
            int ni = 0; while (*ptr && *ptr != '\n' && ni < 15) e.name[ni++] = *ptr++;
            e.name[ni] = 0; if (*ptr == '\n') ptr++;
            if (!found && name_eq(e.name, target)) { res = e; found = 1; }
        }
        if (resp.data1 == 0) break;
    }
    if (found && out) *out = res; return found ? 0 : -1;
}

static int resolve_target(const char *input, uint16_t *out_dir, char *out_name)
{
    if (!input || !*input) return -1;
    char tmp[128]; strncpy(tmp, input, 127); tmp[127] = 0;
    int len = strlen(tmp);
    while (len > 1 && tmp[len-1] == '/') tmp[--len] = 0;
    if (!tmp[0] || (tmp[0] == '/' && !tmp[1])) return -1;
    if (strstr(tmp, "/")) {
        if (qk_path_resolve_ext(current_dir_cluster, tmp, out_dir, out_name) != 0) return -1;
        if (out_name) out_name[15] = 0;
    } else {
        if (out_dir)  *out_dir = current_dir_cluster;
        if (out_name) { strncpy(out_name, tmp, 15); out_name[15] = 0; }
    }
    return 0;
}

/* ── file I/O ─────────────────────────────────────────────────────────────── */

static void *read_file(const char *filename, uint32_t *out_size)
{
    if (storage_pid < 0) return 0;
    uint16_t dc = current_dir_cluster; char rn[16];
    const char *tn = filename;
    if (strstr(filename, "/")) {
        if (qk_path_resolve_ext(current_dir_cluster, filename, &dc, rn) != 0) return 0;
        tn = rn;
    }
    message_t req; memset(&req, 0, sizeof(req));
    req.type = MSG_FILE_OPEN; req.data1 = dc;
    strncpy((char *)req.payload, tn, 12);
    qk_sys_ipc_send(storage_pid, &req);
    message_t resp;
    while (1) { qk_ipc_recv(&resp); if (resp.sender_pid == storage_pid) break; }
    if (resp.type != MSG_FILE_SIZE) return 0;
    uint32_t size = resp.data1; *out_size = size;
    uint8_t *buf = malloc(size); if (!buf) return 0;
    uint32_t off = 0;
    while (off < size) {
        memset(&req, 0, sizeof(req)); req.type = MSG_REQ_DATA; req.data1 = off; req.data2 = 480;
        qk_sys_ipc_send(storage_pid, &req);
        while (1) { qk_ipc_recv(&resp); if (resp.sender_pid == storage_pid && resp.type == MSG_FILE_DATA) break; }
        uint32_t got = resp.data1; if (!got) break;
        memcpy(buf+off, resp.payload, got); off += got;
    }
    return buf;
}

static int save_file(const char *path, const char *data, uint32_t size)
{
    uint16_t dc = current_dir_cluster; char en[16];
    if (resolve_target(path, &dc, en) != 0) return -1;
    return storage_send_file(storage_pid, dc, en, (const uint8_t *)data, size);
}

static void spawn_app(const char *path, const char *arg)
{
    uint32_t sz = 0; void *buf = read_file(path, &sz);
    if (!buf || !sz) { tty_print("Failed to load app.\n"); if (buf) free(buf); return; }
    int pid = qk_spawn(buf, sz); free(buf);
    if (pid >= 0 && arg && *arg) {
        message_t msg; msg.type = MSG_FILE_OPEN;
        strncpy((char *)msg.payload, arg, 127);
        qk_sys_ipc_send(pid, &msg);
    }
}

/* ── command handlers ─────────────────────────────────────────────────────── */

static void cmd_help(const char *args)
{
    (void)args;
    tty_print(C_CYAN "  Files  " C_RESET "  ls  cd <path>  cat <f>  hex <f>  touch <f>  mkdir <d>\n");
    tty_print(C_CYAN "  Editor " C_RESET "  quill <f>\n");
    tty_print(C_CYAN "  Apps   " C_RESET "  notepad  imgview  filemgr  procman  paint  vkbd\n");
    tty_print(C_CYAN "  System " C_RESET "  procs  kill <pid>  exec <f>  uptime  clear\n");
    tty_print(C_CYAN "  Env    " C_RESET "  env  setenv K=V  getenv K\n");
}

static void cmd_ls(const char *args)
{
    (void)args;
    message_t req; memset(&req, 0, sizeof(req));
    req.type = MSG_DIR_REQ; req.data1 = current_dir_cluster;
    qk_sys_ipc_send(storage_pid, &req);
    while (1) {
        message_t resp; qk_ipc_recv(&resp);
        if (resp.sender_pid != storage_pid || resp.type != MSG_DIR_REPLY) continue;
        char *ptr = (char *)resp.payload;
        while (*ptr) {
            char line[80]; int li = 0;
            while (*ptr && *ptr != '\n' && li < 79) line[li++] = *ptr++; line[li] = 0;
            if (*ptr == '\n') ptr++;
            if (!li) continue;
            int is_dir = (line[0] == '1');
            const char *name = line;
            while (*name && *name != '|') name++; if (*name == '|') name++;
            while (*name && *name != '|') name++; if (*name == '|') name++;
            if (is_dir) tty_print("  " C_CYAN "d" C_RESET "  ");
            else        tty_print(C_DIM "  -  " C_RESET);
            tty_print(name); tty_print("\n");
        }
        if (resp.data1 == 0) break;
    }
}

static void cmd_cd(const char *target)
{
    if (!*target || my_strcmp(target, "/") == 0) {
        dir_depth = 0; dir_stack[0] = 0; current_dir_cluster = 0; rebuild_path(); return;
    }
    if (my_strcmp(target, "..") == 0) {
        if (dir_depth > 0) dir_depth--;
        current_dir_cluster = dir_stack[dir_depth]; rebuild_path(); return;
    }
    dir_entry_info_t e;
    if (dir_lookup(current_dir_cluster, target, &e) != 0 || !e.is_dir) {
        tty_print("cd: not found.\n"); return;
    }
    dir_depth++; dir_stack[dir_depth] = e.cluster;
    strncpy(dir_names[dir_depth], e.name, 15);
    current_dir_cluster = e.cluster; rebuild_path();
}

static void cmd_cat(const char *fn)
{
    if (!*fn) { tty_print("cat: need a filename.\n"); return; }
    uint32_t sz = 0; char *data = read_file(fn, &sz);
    if (!data || !sz) { tty_print("cat: not found.\n"); if (data) free(data); return; }
    char line[128]; int li = 0;
    for (uint32_t i = 0; i <= sz; i++) {
        char c = (i < sz) ? data[i] : '\n';
        if (c == '\n') { line[li] = 0; tty_print(line); tty_print("\n"); li = 0; }
        else if (li < 127) line[li++] = c;
    }
    free(data);
}

static void cmd_hex(const char *fn)
{
    if (!*fn) { tty_print("hex: need a filename.\n"); return; }
    uint32_t sz = 0; uint8_t *data = read_file(fn, &sz);
    if (!data || !sz) { tty_print("hex: not found.\n"); if (data) free(data); return; }
    const char *hc = "0123456789ABCDEF";
    for (uint32_t off = 0; off < sz; off += 16) {
        char line[80]; int li = 0;
        char hx[16]; uint_to_hex(off, hx); int hl = strlen(hx);
        for (int k = 0; k < hl; k++) line[li++] = hx[k];
        line[li++] = ':'; line[li++] = ' ';
        for (int i = 0; i < 16; i++) {
            if (off+i < sz) { uint8_t b = data[off+i]; line[li++] = hc[b>>4]; line[li++] = hc[b&0xF]; line[li++] = ' '; }
            else { line[li++] = ' '; line[li++] = ' '; line[li++] = ' '; }
        }
        line[li++] = '|';
        for (int i = 0; i < 16 && off+i < sz; i++) {
            uint8_t b = data[off+i]; line[li++] = (b >= 32 && b <= 126) ? (char)b : '.';
        }
        line[li++] = '|'; line[li] = 0; tty_print(line); tty_print("\n");
    }
    free(data);
}

static void cmd_touch(const char *path)
{
    if (!*path) { tty_print("touch: need a name.\n"); return; }
    uint16_t dc = current_dir_cluster; char en[16];
    if (resolve_target(path, &dc, en) != 0) { tty_print("touch: bad path.\n"); return; }
    if (storage_send_file(storage_pid, dc, en, 0, 0) == 0) tty_print("touch: done.\n");
    else tty_print("touch: failed.\n");
}

static void cmd_mkdir(const char *path)
{
    if (!*path) { tty_print("mkdir: need a name.\n"); return; }
    uint16_t dc = current_dir_cluster; char en[16];
    if (resolve_target(path, &dc, en) != 0) { tty_print("mkdir: bad path.\n"); return; }
    message_t msg; memset(&msg, 0, sizeof(msg));
    msg.type = MSG_DIR_CREATE; msg.data1 = dc;
    strncpy((char *)msg.payload, en, 63); msg.payload[63] = 0;
    qk_sys_ipc_send(storage_pid, &msg);
    message_t resp;
    while (1) { qk_ipc_recv(&resp); if (resp.sender_pid == storage_pid) break; }
    if (resp.type == MSG_OK) tty_print("mkdir: done.\n");
    else tty_print("mkdir: failed.\n");
}

static void cmd_exec(const char *path)
{
    if (!*path) { tty_print("exec: need a path.\n"); return; }
    uint32_t sz = 0; void *buf = read_file(path, &sz);
    if (!buf || !sz) { tty_print("exec: cannot read.\n"); if (buf) free(buf); return; }
    int pid = qk_spawn(buf, sz); free(buf);
    if (pid < 0) { tty_print("exec: failed.\n"); return; }
    char nb[32]; int_to_str(pid, nb, 0);
    tty_print("exec: pid "); tty_print(nb); tty_print("\n");
}

static void cmd_procs(const char *args)
{
    (void)args;
    process_info_t info[32]; int count = qk_get_procs(info, 32);
    tty_print(C_DIM "  PID   STATE      NAME\n");
    tty_print("  ---   --------   ----------------" C_RESET "\n");
    for (int i = 0; i < count; i++) {
        const char *st = "unknown "; const char *sc = C_RESET;
        if      (info[i].state == 0) { st = "ready   "; sc = C_RESET;  }
        else if (info[i].state == 1) { st = "running "; sc = C_GREEN;  }
        else if (info[i].state == 2) { st = "blocked "; sc = C_YELLOW; }
        else if (info[i].state == 3) { st = "zombie  "; sc = C_RED;    }
        else if (info[i].state == 4) { st = "wait_irq"; sc = C_DIM;    }
        char ps[8]; int_to_str(info[i].pid, ps, 0);
        char pid3[4] = {' ', ' ', ' ', 0};
        int pl = strlen(ps); for (int j = 0; j < pl && j < 3; j++) pid3[3-pl+j] = ps[j];
        tty_print("  "); tty_print(pid3); tty_print("   ");
        tty_print(sc); tty_print(st); tty_print(C_RESET);
        tty_print("   "); tty_print(info[i].name); tty_print("\n");
    }
}

static void cmd_uptime(const char *args)
{
    (void)args;
    char nb[32]; int_to_str(qk_get_uptime(), nb, 0);
    tty_print("Uptime: "); tty_print(nb); tty_print(" ms\n");
}

static void cmd_clear(const char *args) { (void)args; tty_clear(); }

static void cmd_kill(const char *args)
{
    if (!*args) { tty_print("kill: need a pid.\n"); return; }
    int pid = 0, sig = 9;
    const char *p = args;
    if (*p == '-') { p++; sig = 0; while (*p >= '0' && *p <= '9') sig = sig*10 + (*p++ - '0'); while (*p == ' ') p++; }
    while (*p >= '0' && *p <= '9') pid = pid*10 + (*p++ - '0');
    if (pid > 0) qk_signal_send(pid, sig);
    else tty_print("kill: invalid pid.\n");
}

static void cmd_env(const char *args)
{
    (void)args;
    char val[128];
    if (qk_getenv("HOME",  val, sizeof(val)) == 0) { tty_print("HOME=");  tty_print(val); tty_print("\n"); }
    if (qk_getenv("PATH",  val, sizeof(val)) == 0) { tty_print("PATH=");  tty_print(val); tty_print("\n"); }
    if (qk_getenv("SHELL", val, sizeof(val)) == 0) { tty_print("SHELL="); tty_print(val); tty_print("\n"); }
}

static void cmd_setenv(const char *args)
{
    if (!*args) { tty_print("setenv: use KEY=VALUE.\n"); return; }
    if (qk_setenv(args) == 0) tty_print("setenv: ok.\n");
    else tty_print("setenv: use KEY=VALUE.\n");
}

static void cmd_getenv(const char *args)
{
    if (!*args) { tty_print("getenv: need a key.\n"); return; }
    char val[128];
    if (qk_getenv(args, val, sizeof(val)) == 0) { tty_print(args); tty_print("="); tty_print(val); tty_print("\n"); }
    else tty_print("getenv: not set.\n");
}

static void cmd_notepad(const char *args) { spawn_app("/APPS/NOTEPAD.QEX", *args ? args : 0); }
static void cmd_imgview(const char *args)
{
    char ip[128];
    if (!*args) { spawn_app("/APPS/IMGVIEW.QEX", 0); return; }
    if (args[0] == '/') {
        strncpy(ip, args, 127); ip[127] = 0;
    } else {
        int pl = strlen(current_path);
        strncpy(ip, current_path, 127); ip[127] = 0;
        if (pl > 0 && pl < 127 && ip[pl-1] != '/') ip[pl++] = '/';
        strncpy(ip+pl, args, 127-pl); ip[127] = 0;
    }
    int ips = strlen(ip), has_dot = 0;
    for (int d = ips-1; d >= 0 && ip[d] != '/'; d--) if (ip[d] == '.') { has_dot = 1; break; }
    if (!has_dot && ips+4 < 128) { ip[ips++]='.'; ip[ips++]='B'; ip[ips++]='M'; ip[ips++]='P'; ip[ips]=0; }
    spawn_app("/APPS/IMGVIEW.QEX", ip);
}
static void cmd_filemgr(const char *args) { (void)args; spawn_app("/APPS/FILEMGR.QEX", 0); }
static void cmd_procman(const char *args) { (void)args; spawn_app("/APPS/PROCMAN.QEX", 0); }
static void cmd_paint  (const char *args) { (void)args; spawn_app("/APPS/PAINT.QEX",   0); }
static void cmd_vkbd   (const char *args) { (void)args; spawn_app("/APPS/VKBD.QEX",    0); }

/* ── quill (blocking sub-mode) ────────────────────────────────────────────── */

static int read_line_blocking(char *out, int max_len, uint8_t *special_sc)
{
    int ptr = 0; out[0] = 0;
    if (special_sc) *special_sc = 0;
    uint8_t awaiting_ext = 0; int ctrl_down = 0;
    while (1) {
        message_t msg; qk_ipc_recv(&msg);
        if (msg.type != MSG_KEY_EVENT) continue;
        uint32_t raw = msg.data1;
        uint8_t is_rel = (raw & 0x80) != 0;
        uint8_t code   = (uint8_t)(raw & 0x7F);
        int ext = (raw & 0x100) != 0;
        if (!ext && code == 0x00) { awaiting_ext = 1; continue; }
        if (is_rel) { if (code == SCANCODE_CTRL) ctrl_down = 0; awaiting_ext = 0; continue; }
        if (code == SCANCODE_CTRL) { ctrl_down = 1; continue; }
        if (awaiting_ext) {
            awaiting_ext = 0;
            if (code == SCANCODE_ARROW_UP || code == SCANCODE_ARROW_DOWN) {
                if (special_sc) *special_sc = code; return 1;
            }
            continue;
        }
        if (code == SCANCODE_ARROW_UP || code == SCANCODE_ARROW_DOWN) {
            if (special_sc) *special_sc = code; return 1;
        }
        if (ctrl_down && code == 0x2E) { if (special_sc) *special_sc = SPECIAL_CTRL_C; return 2; }
        char c = scancode_to_char(code, msg.data2);
        if (c == '\n') { tty_print("\n"); out[ptr] = 0; return 0; }
        if (c == '\b') { if (ptr > 0) { ptr--; out[ptr] = 0; tty_print("\b"); } continue; }
        if (c && ptr < max_len-1) { out[ptr++] = c; out[ptr] = 0; char t[2] = {c, 0}; tty_print(t); }
    }
}

static void cmd_quill(const char *name)
{
    if (!*name) { tty_print("quill: need a file name.\n"); return; }
    uint32_t sz = 0; char *existing = read_file(name, &sz);
    char *buffer = malloc(NANO_BUFFER_SIZE);
    if (!buffer) { if (existing) free(existing); return; }
    buffer[0] = 0;
    if (existing && sz > 0) {
        uint32_t cp = sz < (uint32_t)(NANO_BUFFER_SIZE-1) ? sz : (uint32_t)(NANO_BUFFER_SIZE-1);
        memcpy(buffer, existing, cp); buffer[cp] = 0;
    }
    if (existing) free(existing);
    tty_print("quill: .save to save, .quit to exit\n");
    if (buffer[0]) {
        char lbuf[128]; int ll = 0;
        for (int i = 0; buffer[i]; i++) {
            if (buffer[i] == '\n') { lbuf[ll] = 0; tty_print(lbuf); tty_print("\n"); ll = 0; }
            else if (ll < 127) lbuf[ll++] = buffer[i];
        }
        if (ll) { lbuf[ll] = 0; tty_print(lbuf); }
    }
    char line[128];
    while (1) {
        tty_print("[quill] ");
        uint8_t sp = 0; int r = read_line_blocking(line, sizeof(line), &sp);
        if (r == 2 && sp == SPECIAL_CTRL_C) { tty_print("\nquill: exit.\n"); break; }
        if (r == 1) { tty_print("\n"); continue; }
        if (my_strcmp(line, ".save") == 0 || my_strcmp(line, ".safe") == 0) {
            if (save_file(name, buffer, strlen(buffer)) == 0) tty_print("quill: saved.\n");
            else tty_print("quill: save failed.\n");
        } else if (my_strcmp(line, ".quit") == 0) {
            break;
        } else if (my_strcmp(line, ".clear") == 0) {
            buffer[0] = 0;
        } else {
            int bl = strlen(buffer), ll2 = strlen(line);
            if (bl + ll2 + 2 < NANO_BUFFER_SIZE) { strcat(buffer, line); strcat(buffer, "\n"); }
        }
    }
    free(buffer);
}

/* ── dispatch table ───────────────────────────────────────────────────────── */

typedef void (*cmd_fn_t)(const char *args);
typedef struct { const char *name; const char *alias; cmd_fn_t fn; } cmd_t;

static const cmd_t cmd_table[] = {
    {"help",    "guide",   cmd_help},
    {"ls",      "glimpse", cmd_ls},
    {"cd",      "drift",   cmd_cd},
    {"cat",     "beam",    cmd_cat},
    {"hex",     "quartz",  cmd_hex},
    {"touch",   "seed",    cmd_touch},
    {"mkdir",   "hatch",   cmd_mkdir},
    {"exec",    0,         cmd_exec},
    {"procs",   0,         cmd_procs},
    {"uptime",  0,         cmd_uptime},
    {"clear",   "purge",   cmd_clear},
    {"kill",    0,         cmd_kill},
    {"env",     0,         cmd_env},
    {"setenv",  0,         cmd_setenv},
    {"getenv",  0,         cmd_getenv},
    {"quill",   "nano",    cmd_quill},
    {"notepad", 0,         cmd_notepad},
    {"imgview", 0,         cmd_imgview},
    {"filemgr", 0,         cmd_filemgr},
    {"procman", 0,         cmd_procman},
    {"paint",   0,         cmd_paint},
    {"vkbd",    0,         cmd_vkbd},
};
#define CMD_TABLE_LEN ((int)(sizeof(cmd_table) / sizeof(cmd_table[0])))

/* ── command dispatch ─────────────────────────────────────────────────────── */

static void run_command(const char *line)
{
    while (*line == ' ') line++;
    if (!*line) return;

    /* split into name + args in one pass */
    char name[32]; int nlen = 0;
    while (line[nlen] && line[nlen] != ' ' && nlen < 31) nlen++;
    memcpy(name, line, nlen); name[nlen] = 0;
    const char *args = line[nlen] ? line + nlen + 1 : line + nlen;

    for (int i = 0; i < CMD_TABLE_LEN; i++) {
        if (my_strcmp(name, cmd_table[i].name) == 0 ||
            (cmd_table[i].alias && my_strcmp(name, cmd_table[i].alias) == 0)) {
            cmd_table[i].fn(args);
            return;
        }
    }
    tty_print(C_RED "  reactty: command not found: " C_RESET);
    tty_print(name); tty_print("\n");
}

static void show_prompt(void)
{
    tty_print(C_CYAN); tty_print(current_path); tty_print(C_RESET " > ");
}

/* ── readline ─────────────────────────────────────────────────────────────── */

typedef struct {
    char buf[CMD_BUF_SIZE];
    int  len;
    int  ctrl_down;
    int  awaiting_ext;
} readline_t;

static void readline_reset(readline_t *rl)
{
    memset(rl->buf, 0, CMD_BUF_SIZE);
    rl->len = rl->ctrl_down = rl->awaiting_ext = 0;
}

/* Returns 1 when a complete line is ready; sets *ctrl_c if Ctrl+C was pressed. */
static int readline_feed(readline_t *rl, uint32_t raw, uint32_t mods, int *ctrl_c)
{
    *ctrl_c = 0;
    bool    is_rel = (raw & 0x80) != 0;
    uint8_t sc     = (uint8_t)(raw & 0x7F);
    int     ext    = (raw & 0x100) != 0;

    if (!ext && sc == 0x00) { rl->awaiting_ext = 1; return 0; }
    if (is_rel) { if (sc == SCANCODE_CTRL) rl->ctrl_down = 0; rl->awaiting_ext = 0; return 0; }
    if (sc == SCANCODE_CTRL) { rl->ctrl_down = 1; return 0; }
    if (rl->awaiting_ext || ext) { rl->awaiting_ext = 0; return 0; } /* ignore arrows etc. */

    if (rl->ctrl_down && sc == 0x2E) { *ctrl_c = 1; return 1; }

    char c = scancode_to_char(sc, mods);
    if (!c) return 0;

    if (c == '\n') { tty_print("\n"); return 1; }
    if (c == '\b') {
        if (rl->len > 0) { rl->len--; rl->buf[rl->len] = 0; tty_print("\b"); }
        return 0;
    }
    if (rl->len < CMD_BUF_SIZE - 1) {
        rl->buf[rl->len++] = c; rl->buf[rl->len] = 0;
        char t[2] = {c, 0}; tty_print(t);
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
   MAIN
   ═══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    storage_pid = qk_get_server_pid(QK_SERVER_STORAGE);
    qk_set_proc_name(-1, "reactty");

    while (master_pid < 0) {
        message_t msg; qk_ipc_recv(&msg);
        if (msg.type == MSG_TERM_ATTACH) master_pid = (int)msg.data1;
    }

    dir_depth = 0; dir_stack[0] = 0; current_dir_cluster = 0;
    rebuild_path();
    cmd_cd("/USERS/DEFAULT");
    qk_setenv("HOME=/USERS/DEFAULT");
    qk_setenv("PATH=/APPS");
    qk_setenv("SHELL=reactty");

    tty_print(C_BCYAN "QuarkOS reactty" C_RESET "  --  type 'help' for commands\n");
    show_prompt();

    readline_t rl; readline_reset(&rl);

    while (1) {
        message_t msg; qk_ipc_recv(&msg);
        if (msg.type != MSG_KEY_EVENT) continue;

        int ctrl_c = 0;
        if (!readline_feed(&rl, msg.data1, msg.data2, &ctrl_c)) continue;

        if (ctrl_c) tty_print("^C\n");
        else        run_command(rl.buf);

        readline_reset(&rl);
        show_prompt();
    }
    return 0;
}
