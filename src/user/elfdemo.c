#include "kernel/ipc.h"
#include "lib/stdlib.h"
#include "user/lib/libapi.h"
#define WIN_W 380
#define WIN_H 430
#define C_BG 0xFF1E2127
#define C_ROW_A 0xFF21252B
#define C_BLUE 0xFF61AFEF
#define C_GREEN 0xFF98C379
#define C_YELLOW 0xFFD19A66
#define C_PURPLE 0xFFC678DD
#define C_RED 0xFFE06C75
#define C_WHITE 0xFFD7D7D7
#define C_GREY 0xFF5C6370
typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;
static int comp_pid = -1;
static void rect(int x, int y, int w, int h, uint32_t col)
{
    message_t m;
    m.type = MSG_WIN_DRAW_RECT;
    m.data1 = y;
    m.data2 = x;
    m.data3 = w;
    *(int *)&m.payload[0] = h;
    *(uint32_t *)&m.payload[4] = col;
    qk_sys_ipc_send(comp_pid, &m);
}
static void row(int y, uint32_t accent, const char *key, const char *val, uint32_t vcol)
{
    rect(0, y, WIN_W, 22, C_ROW_A);
    rect(0, y, 4, 22, accent);
    qk_print_at(14, y + 5, C_GREY, key);
    qk_print_at(160, y + 5, vcol, val);
}
static void gap(int y)
{
    rect(0, y, WIN_W, 10, C_BG);
}
static void pad(int y)
{
    rect(0, y, WIN_W, 6, C_ROW_A);
}
static void num_str(char *buf, uint32_t v)
{
    char tmp[12];
    int_to_str((int)v, tmp, 10);
    strcat(buf, tmp);
}
static void hex_str(char *buf, uint32_t v)
{
    static const char h[] = "0123456789ABCDEF";
    strcat(buf, "0x");
    int s = strlen(buf);
    buf[s + 8] = 0;
    for (int i = 7; i >= 0; i--) {
        buf[s + i] = h[v & 0xF];
        v >>= 4;
    }
}
static void draw_ui(void)
{
    rect(0, 0, WIN_W, WIN_H, C_BG);
    const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)0x400000;
    int y = 8;
    row(y, C_BLUE, "Type", "ELF32 Executable", C_WHITE);
    y += 22;
    row(y, C_BLUE, "Arch", "IA-32  (x86)", C_WHITE);
    y += 22;
    row(y, C_BLUE, "Entry", "0x00400000", C_YELLOW);
    y += 22;
    row(y, C_BLUE, "Load base", "0x00400000", C_YELLOW);
    y += 22;
    pad(y);
    y += 6;
    gap(y);
    y += 10;
    row(y, C_GREEN, "PT_LOAD [0]", ".text + .data   R-X", C_GREEN);
    y += 22;
    row(y, C_GREEN, "PT_LOAD [1]", ".bss            RW-", C_YELLOW);
    y += 22;
    row(y, C_GREEN, "Stack", "0xAFF00000-0xB0000000", C_GREY);
    y += 22;
    pad(y);
    y += 6;
    gap(y);
    y += 10;
    process_info_t procs[16];
    int count = qk_get_procs(procs, 16);
    int my_pid = -1;
    for (int i = 0; i < count; i++) {
        const char *want = "elfdemo";
        int ok = 1;
        for (int j = 0; j < 7; j++)
            if (procs[i].name[j] != want[j]) {
                ok = 0;
                break;
            }
        if (ok) {
            my_pid = procs[i].pid;
            break;
        }
    }
    char pid_s[8];
    pid_s[0] = 0;
    num_str(pid_s, my_pid >= 0 ? my_pid : 0);
    row(y, C_PURPLE, "PID", pid_s, C_PURPLE);
    y += 22;
    uint32_t uptime_s = qk_get_uptime() / 1000;
    char up_s[16];
    up_s[0] = 0;
    num_str(up_s, uptime_s);
    strcat(up_s, " s");
    row(y, C_PURPLE, "Uptime", up_s, C_PURPLE);
    y += 22;
    pad(y);
    y += 6;
    gap(y);
    y += 10;
    char buf[32];
    row(y, C_RED, "Magic bytes", "7F 45 4C 46", C_RED);
    y += 22;
    buf[0] = 0;
    hex_str(buf, ehdr->e_entry);
    row(y, C_RED, "Entry point", buf, C_YELLOW);
    y += 22;
    buf[0] = 0;
    hex_str(buf, ehdr->e_phoff);
    row(y, C_RED, "PHDR offset", buf, C_WHITE);
    y += 22;
    buf[0] = 0;
    num_str(buf, ehdr->e_phnum);
    row(y, C_RED, "Segments", buf, C_WHITE);
    y += 22;
    buf[0] = 0;
    num_str(buf, ehdr->e_shnum);
    row(y, C_RED, "Sections", buf, C_WHITE);
    y += 22;
    buf[0] = 0;
    num_str(buf, ehdr->e_ehsize);
    strcat(buf, " bytes");
    row(y, C_RED, "Header size", buf, C_GREY);
    y += 22;
    pad(y);
}
int main(void)
{
    qk_set_proc_name(-1, "elfdemo");
    comp_pid = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    if (comp_pid < 0)
        return 1;
    message_t msg;
    msg.type = MSG_WIN_CREATE;
    msg.data1 = WIN_W;
    msg.data2 = WIN_H;
    strncpy((char *)msg.payload, "ELF Loader Demo", 64);
    qk_sys_ipc_send(comp_pid, &msg);
    while (1) {
        qk_ipc_recv(&msg);
        if (msg.type == MSG_WIN_CREATED)
            break;
    }
    while (1) {
        while (qk_ipc_recv_nb(&msg) == 0) {
        }
        message_t clr;
        clr.type = MSG_WIN_CLEAR;
        qk_sys_ipc_send(comp_pid, &clr);
        draw_ui();
        qk_sleep(1000);
    }
    return 0;
}
