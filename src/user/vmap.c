// SPDX-License-Identifier: MIT
/*
 * src/user/vmap.c - Kernel virtual memory map visualizer
 */
#include "kernel/ipc.h"
#include "lib/stdlib.h"
#include "user/lib/libapi.h"
#include "user/lib/window.h"
#define COMP_PID 5
#define WIN_W 460
#define WIN_H 600
#define TITLE_H 32
#define BAR_H 512
#define MARGIN_V 28
#define BAR_TOP MARGIN_V
#define BAR_LEFT 75
#define BAR_W 45
#define COL_BOX 160
#define COL_ADDR 180
#define COL_NAME 290
#define ROW_STEP 22
#define C_KERNEL 0xFFFF4C4C
#define C_USER 0xFF4CFF88
#define C_FB 0xFF4CC9FF
#define C_STACK 0xFFFFD24C
#define C_BAR_BG 0xFF151515
#define C_FRAME 0xFF333333
void v_rect(int x, int y, int w, int h, uint32_t color) {
    message_t msg;
    msg.type = MSG_WIN_DRAW_RECT;
    msg.data1 = y;
    msg.data2 = x;
    msg.data3 = w;
    *(int *)&msg.payload[0] = h;
    *(uint32_t *)&msg.payload[4] = color;
    qk_sys_ipc_send(COMP_PID, &msg);
}
void main() {
    vmap_region_t regions[16];
    int count = qk_get_vmap(regions);
    message_t msg;
    msg.type = MSG_WIN_CREATE;
    msg.data1 = WIN_W;
    msg.data2 = WIN_H;
    strncpy((char *)msg.payload, "Kernel Memory Map", 17);
    qk_sys_ipc_send(COMP_PID, &msg);
    while (1) {
        qk_ipc_recv(&msg);
        if (msg.type == MSG_WIN_CREATED)
            break;
    }
    v_rect(BAR_LEFT - 2, BAR_TOP - 2, BAR_W + 4, BAR_H + 4, C_FRAME);
    v_rect(BAR_LEFT, BAR_TOP, BAR_W, BAR_H, C_BAR_BG);
    for (int gb = 0; gb <= 4; gb++) {
        int y = BAR_TOP + BAR_H - (gb * BAR_H / 4);
        v_rect(BAR_LEFT - 8, y, 6, 1, C_FRAME);
        char label[8] = {0};
        int_to_str(gb, label, 10);
        strcat(label, "G");
        qk_print_at(15, y - 7, 0xFF777777, label);
    }
    int table_y = BAR_TOP + 10;
    for (int i = 0; i < count; i++) {
        uint32_t addr = regions[i].start;
        uint32_t start_mb = addr >> 20;
        uint32_t end_mb = regions[i].end >> 20;
        if (end_mb > 4095)
            end_mb = 4096;
        int h = ((end_mb - start_mb) * BAR_H) / 4096;
        int py = BAR_TOP + (BAR_H * (4096 - end_mb) / 4096);
        uint32_t clr = (addr >= 0xC0000000) ? C_KERNEL : C_USER;
        if (addr == 0x80000000)
            clr = C_FB;
        if (addr == 0xAFF00000)
            clr = C_STACK;
        v_rect(BAR_LEFT, py, BAR_W, h < 2 ? 2 : h, clr);
        v_rect(COL_BOX, table_y + 1, 10, 10, clr);
        char hex[16];
        uint_to_hex(addr, hex);
        qk_print_at(COL_ADDR, table_y, clr, hex);
        qk_print_at(COL_NAME, table_y, 0xFFCCCCCC, regions[i].name);
        table_y += ROW_STEP;
    }
    while (1) {
        qk_sleep(1000);
    }
}
