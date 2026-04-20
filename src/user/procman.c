#include "kernel/ipc.h"
#include "lib/stdbool.h"
#include "lib/stdlib.h"
#include "user/lib/icons_def.h"
#include "user/lib/libapi.h"
#include "user/lib/window.h"
#define WIN_W 400
#define WIN_H 540
#define STATS_H 85
static int comp_pid = -1;
static int selected_pid = -1;
static process_info_t last_procs[14];
static int last_count = 0;
void draw_rect_rpc(int x, int y, int w, int h, uint32_t color)
{
    message_t msg;
    msg.type = MSG_WIN_DRAW_RECT;
    msg.data1 = y;
    msg.data2 = x;
    msg.data3 = w;
    *(int *)&msg.payload[0] = h;
    *(uint32_t *)&msg.payload[4] = color;
    qk_sys_ipc_send(comp_pid, &msg);
}
void draw_icon_rpc(int x, int y, int icon_id, uint32_t color)
{
    message_t msg;
    msg.type = MSG_WIN_PRINT_AT;
    msg.data1 = x;
    msg.data2 = y;
    msg.data3 = color;
    msg.payload[0] = (char)icon_id;
    msg.payload[1] = 0;
    qk_sys_ipc_send(comp_pid, &msg);
}
void render_ui()
{
    uint32_t used_mem, total_mem, cpu_load;
    qk_get_stats(&used_mem, &total_mem, &cpu_load);
    uint32_t uptime = qk_get_uptime() / 1000;
    draw_rect_rpc(0, 0, WIN_W, 50, 0xFF242424);
    draw_icon_rpc(15, 15, ICON_MONITOR, 0xFF9A9A9A);
    qk_print_at(42, 17, 0xFFFFFFFF, "System Activity");
    char up_s[32], t_s[16];
    int_to_str(uptime, t_s, 10);
    strncpy(up_s, "UP: ", 5);
    strcat(up_s, t_s);
    strcat(up_s, "s");
    qk_print_at(WIN_W - 100, 17, 0xFF666666, up_s);
    int sx = 10, sy = 60, sw = WIN_W - 20;
    char cpu_s[32], cpu_v[16];
    int_to_str(cpu_load, cpu_v, 10);
    strncpy(cpu_s, "CPU Load: ", 12);
    strcat(cpu_s, cpu_v);
    strcat(cpu_s, "%");
    qk_print_at(sx + 15, sy + 12, 0xFFAAAAAA, cpu_s);
    draw_rect_rpc(sx + 15, sy + 32, sw - 30, 8, 0xFF333333);
    int cpu_w = ((sw - 30) * cpu_load) / 100;
    draw_rect_rpc(sx + 15, sy + 32, cpu_w, 8, 0xFF888888);
    uint32_t mem_perc = (total_mem > 0) ? (used_mem * 100 / total_mem) : 0;
    char mem_s[48], mem_v[16];
    int_to_str(used_mem / 1024 / 1024, mem_v, 10);
    strncpy(mem_s, "Memory: ", 10);
    strcat(mem_s, mem_v);
    strcat(mem_s, "MB / ");
    int_to_str(total_mem / 1024 / 1024, mem_v, 10);
    strcat(mem_s, mem_v);
    strcat(mem_s, "MB");
    qk_print_at(sx + 15, sy + 50, 0xFFAAAAAA, mem_s);
    draw_rect_rpc(sx + 15, sy + 68, sw - 30, 8, 0xFF333333);
    int mem_w = ((sw - 30) * (int)mem_perc) / 100;
    draw_rect_rpc(sx + 15, sy + 68, mem_w, 8, 0xFF888888);
    int ty = 155;
    draw_rect_rpc(10, ty, WIN_W - 20, 26, 0xFF2A2A2A);
    draw_rect_rpc(10, ty + 25, WIN_W - 20, 1, 0xFF3A3A3A);
    qk_print_at(20, ty + 6, 0xFF9A9A9A, "PID");
    qk_print_at(75, ty + 6, 0xFF9A9A9A, "NAME");
    qk_print_at(275, ty + 6, 0xFF9A9A9A, "STATE");
    last_count = qk_get_procs(last_procs, 14);
    process_info_t *procs = last_procs;
    int count = last_count;
    for (int i = 0; i < count; i++) {
        int ry = 190 + (i * 24);
        bool is_sel = (procs[i].pid == selected_pid);
        uint32_t bg = is_sel ? 0xFF353535 : ((i % 2 == 0) ? 0xFF222222 : 0xFF1E1E1E);
        draw_rect_rpc(10, ry, WIN_W - 20, 22, bg);
        char p_s[12];
        int_to_str(procs[i].pid, p_s, 10);
        uint32_t n_clr = 0xFFAAAAAA;
        const char *st_s = "WAIT";
        uint32_t st_clr = 0xFF666666;
        switch (procs[i].state) {
        case 0:
            st_s = "READY";
            st_clr = 0xFFCCCCCC;
            break;
        case 1:
            st_s = "RUN";
            st_clr = 0xFFFFFFFF;
            break;
        case 4:
            st_s = "IRQ";
            st_clr = 0xFF888888;
            break;
        }
        qk_print_at(20, ry + 5, 0xFF9A9A9A, p_s);
        qk_print_at(75, ry + 5, n_clr, (procs[i].name[0] ? procs[i].name : "sys_task"));
        qk_print_at(275, ry + 5, st_clr, st_s);
        if (is_sel && procs[i].pid != 0) {
            draw_rect_rpc(WIN_W - 48, ry + 2, 32, 18, 0xFFE06C75);
            draw_icon_rpc(WIN_W - 40, ry + 3, ICON_CLOSE, 0xFFFFFFFF);
        }
    }
    draw_rect_rpc(0, WIN_H - 30, WIN_W, 30, 0xFF242424);
    qk_print_at(10, WIN_H - 20, 0xFF666666, "Click a row to select. Press [X] to Kill.");
}
int main()
{
    comp_pid = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    if (comp_pid < 0)
        return 1;
    qk_set_proc_name(-1, "procman");
    message_t msg;
    msg.type = MSG_WIN_CREATE;
    msg.data1 = WIN_W;
    msg.data2 = WIN_H;
    strncpy((char *)msg.payload, "Task Manager", 64);
    qk_sys_ipc_send(comp_pid, &msg);
    while (1) {
        qk_ipc_recv(&msg);
        if (msg.type == MSG_WIN_CREATED)
            break;
    }
    while (1) {
        while (qk_ipc_recv_nb(&msg) == 0) {
            if (msg.type == MSG_MOUSE_EVENT && msg.data1 == EVENT_MOUSE_DOWN) {
                int mx = msg.data2;
                int my = msg.data3 - (TITLE_BAR_HEIGHT + WIN_CONTENT_GAP);
                if (my >= 190 && my < 190 + (last_count * 24)) {
                    int idx = (my - 190) / 24;
                    if (idx >= 0 && idx < last_count) {
                        if (selected_pid == last_procs[idx].pid && mx > WIN_W - 55 &&
                            last_procs[idx].pid != 0) {
                            qk_sys_kill(last_procs[idx].pid);
                            selected_pid = -1;
                        } else {
                            selected_pid = last_procs[idx].pid;
                        }
                    }
                } else {
                    selected_pid = -1;
                }
            }
        }
        message_t clr;
        clr.type = MSG_WIN_CLEAR;
        qk_sys_ipc_send(comp_pid, &clr);
        render_ui();
        qk_sleep(250);
    }
    return 0;
}
