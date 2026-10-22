#include "user/bin/command.h"

void command_main(const char *args) {
    (void)args;
    char val[128];
    if (qk_getenv("HOME", val, sizeof(val)) == 0) {
        tty_print("HOME=");
        tty_print(val);
        tty_print("\n");
    }
    if (qk_getenv("PATH", val, sizeof(val)) == 0) {
        tty_print("PATH=");
        tty_print(val);
        tty_print("\n");
    }
    if (qk_getenv("SHELL", val, sizeof(val)) == 0) {
        tty_print("SHELL=");
        tty_print(val);
        tty_print("\n");
    }
}
