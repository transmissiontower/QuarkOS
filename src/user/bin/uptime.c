#include "user/bin/command.h"

void command_main(const char *args) {
    (void)args;
    char nb[32];
    int_to_str(qk_get_uptime(), nb, 0);
    tty_print("Uptime: ");
    tty_print(nb);
    tty_print(" ms\n");
}
