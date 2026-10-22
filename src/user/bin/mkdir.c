#include "user/bin/command.h"

void command_main(const char *path) {
    if (!*path) {
        tty_print("mkdir: need a name.\n");
        return;
    }
    uint32_t dc = current_dir_cluster;
    char en[64];
    if (resolve_target(path, &dc, en) != 0) {
        tty_print("mkdir: bad path.\n");
        return;
    }
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_DIR_CREATE;
    msg.data1 = dc;
    strncpy((char *)msg.payload, en, 63);
    msg.payload[63] = 0;
    qk_sys_ipc_send(storage_pid, &msg);
    message_t resp;
    while (1) {
        qk_ipc_recv(&resp);
        if (resp.sender_pid == storage_pid)
            break;
    }
    if (resp.type == MSG_OK)
        tty_print("mkdir: done.\n");
    else
        tty_print("mkdir: failed.\n");
}
