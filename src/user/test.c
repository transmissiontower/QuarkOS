// SPDX-License-Identifier: MIT
/*
 * src/user/test.c - Plasma effect demo window using sine-wave color animation
 */
#include "kernel/ipc.h"
#include "lib/math.h"
#include "lib/stdlib.h"
#include "user/lib/libapi.h"
#define WIN_W 160
#define WIN_H 100
static int comp_pid = -1;
static int ensure_comp_pid(void)
{
    if (comp_pid < 0)
        comp_pid = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    return (comp_pid < 0) ? -1 : 0;
}
uint32_t palette[256];
int sine_table[512];
void init_plasma_logic()
{
    for (int i = 0; i < 256; i++) {
        uint8_t r = (uint8_t)((sin(i * PI / 32.0) + 1.0) * 127.5);
        uint8_t g = (uint8_t)((sin(i * PI / 64.0 + PI / 4.0) + 1.0) * 127.5);
        uint8_t b = (uint8_t)((sin(i * PI / 128.0) + 1.0) * 127.5);
        palette[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
    for (int i = 0; i < 512; i++) {
        sine_table[i] = (int)(sin(i * PI / 64.0) * 32.0);
    }
}
int main()
{
    if (ensure_comp_pid() != 0)
        return 1;
    message_t msg;
    msg.type = MSG_WIN_CREATE;
    msg.data1 = WIN_W;
    msg.data2 = WIN_H;
    strncpy((char *)msg.payload, "Plasma Window", 14);
    qk_sys_ipc_send(comp_pid, &msg);
    while (1) {
        qk_ipc_recv(&msg);
        if (msg.type == MSG_WIN_CREATED)
            break;
    }
    init_plasma_logic();
    int time_shift = 0;
    while (1) {
        time_shift += 2;
        for (int y = 0; y < WIN_H; y++) {
            int y_comp = sine_table[(y + time_shift) & 511];
            for (int x = 0; x < WIN_W; x += 32) {
                message_t line;
                line.type = MSG_WIN_DRAW_LINE;
                line.data1 = y;
                line.data2 = x;
                line.data3 = 32;
                uint32_t *colors = (uint32_t *)line.payload;
                for (int i = 0; i < 32; i++) {
                    colors[i] =
                        palette[(uint8_t)(sine_table[(x + i + time_shift) & 511] + y_comp + 128)];
                }
                qk_sys_ipc_send(comp_pid, &line);
            }
        }
        qk_sleep(1);
    }
}
