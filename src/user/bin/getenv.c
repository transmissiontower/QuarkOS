#include "user/bin/command.h"

void command_main(const char *args) {
    if (!*args) {
        tty_print("getenv: need a key.\n");
        return;
    }
    char val[128];
    if (qk_getenv(args, val, sizeof(val)) == 0) {
        tty_print(args);
        tty_print("=");
        tty_print(val);
        tty_print("\n");
    } else
        tty_print("getenv: not set.\n");
}
