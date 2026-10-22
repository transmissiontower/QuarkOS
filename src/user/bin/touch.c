#include "user/bin/command.h"

void command_main(const char *path) {
    if (!*path) {
        tty_print("touch: need a name.\n");
        return;
    }
    uint32_t dc = current_dir_cluster;
    char en[64];
    if (resolve_target(path, &dc, en) != 0) {
        tty_print("touch: bad path.\n");
        return;
    }
    if (storage_send_file(storage_pid, dc, en, 0, 0, 0) == 0)
        tty_print("touch: done.\n");
    else
        tty_print("touch: failed.\n");
}
