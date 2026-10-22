#include "user/bin/command.h"

void command_main(const char *path) {
    if (!*path) {
        tty_print("exec: need a path.\n");
        return;
    }
    uint32_t sz = 0;
    void *buf = read_file(path, &sz);
    if (!buf || !sz) {
        tty_print("exec: cannot read.\n");
        if (buf)
            free(buf);
        return;
    }
    int pid = qk_spawn(buf, sz);
    free(buf);
    if (pid < 0) {
        tty_print("exec: failed.\n");
        return;
    }
    char nb[32];
    int_to_str(pid, nb, 0);
    tty_print("exec: pid ");
    tty_print(nb);
    tty_print("\n");
}
