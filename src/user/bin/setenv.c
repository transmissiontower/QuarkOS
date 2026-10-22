#include "user/bin/command.h"

void command_main(const char *args) {
    if (!*args) {
        tty_print("setenv: use KEY=VALUE.\n");
        return;
    }
    if (qk_setenv(args) == 0)
        tty_print("setenv: ok.\n");
    else
        tty_print("setenv: use KEY=VALUE.\n");
}
