#include "user/lib/libapi.h"
#include "kernel/ipc.h"
#include "lib/stdlib.h"

static int comp_pid = -1;

static int ensure_comp_pid(void) {
    if (comp_pid >= 0) return 0;
    comp_pid = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    return (comp_pid < 0) ? -1 : 0;
}

static void send_print(const char *text) {
    message_t msg = {0};
    msg.type = MSG_WIN_PRINT;
    int len = strlen(text);
    if (len > 127) len = 127;
    memcpy(msg.payload, text, len);
    msg.payload[len] = 0;
    qk_sys_ipc_send(comp_pid, &msg);
}

static void send_print_empty(void) {
    send_print(" ");
}

int main(void) {
    if (ensure_comp_pid() != 0) return 1;

    message_t msg = {0};
    msg.type = MSG_WIN_CREATE;
    msg.data1 = 360;
    msg.data2 = 220;
    strncpy((char*)msg.payload, "ELF Loader Demo", sizeof(msg.payload) - 1);
    qk_sys_ipc_send(comp_pid, &msg);

    while (1) {
        qk_ipc_recv(&msg);
        if (msg.type == MSG_WIN_CREATED) break;
    }

    message_t color = {.type = MSG_WIN_COLOR, .data1 = 0xFFD7D7D7};
    qk_sys_ipc_send(comp_pid, &color);

    send_print("ELF demo process");
    send_print("Loaded as plain ELF (no .qex)");
    send_print_empty();
    send_print("Demonstrates compositor IPC:");
    send_print(" - MSG_WIN_CREATE");
    send_print(" - MSG_WIN_PRINT");
    send_print(" - MSG_WIN_COLOR");
    send_print_empty();
    send_print("Copy *.elf apps into ::APPS");
    send_print("and launch them from shell.");

    while (1) {
        qk_sleep(1000);
    }
    return 0;
}
