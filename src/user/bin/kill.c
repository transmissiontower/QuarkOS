#include "user/bin/command.h"

void command_main(const char *args) {
    if (!*args) {
        tty_print("kill: need a pid.\n");
        return;
    }
    int pid = 0, sig = 9;
    const char *p = args;
    if (*p == '-') {
        p++;
        sig = 0;
        while (*p >= '0' && *p <= '9')
            sig = sig * 10 + (*p++ - '0');
        while (*p == ' ')
            p++;
    }
    while (*p >= '0' && *p <= '9')
        pid = pid * 10 + (*p++ - '0');
    if (pid > 0)
        qk_signal_send(pid, sig);
    else
        tty_print("kill: invalid pid.\n");
}
