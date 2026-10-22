#include "user/bin/command.h"

void command_main(const char *args) {
    int ndisks = qk_disk_count();
    if (!args || !*args) {
        tty_print("  usage: unmnt sd1\n");
        return;
    }
    int idx = -1;
    if (args[0] == 's' && args[1] == 'd' && args[2] >= '0' && args[2] <= '9')
        idx = args[2] - '0';
    else if (args[0] >= '0' && args[0] <= '9')
        idx = args[0] - '0';
    if (idx <= 0 || idx >= ndisks) {
        tty_print("  unmnt: invalid or boot drive.\n");
        return;
    }
    message_t m, r;
    memset(&m, 0, sizeof(m));
    m.type = MSG_DISK_EJECT;
    m.data1 = (uint32_t)idx;
    qk_ipc_send_recv(storage_pid, &m, &r);
    if (r.type == MSG_OK) {
        tty_print("  sd");
        char c[2] = {'0' + idx, 0};
        tty_print(c);
        tty_print(" ejected\n");
        if (current_disk == idx) {
            current_disk = 0;
            current_dir_cluster = 0;
            qk_disk_select(0);
            qk_setenv("PWD=/sd0");
        }
    } else {
        tty_print("  unmnt: failed.\n");
    }
}
