#include "lib/stdlib.h"
#include "util/util.h"
#include "video/window.h"

void command_run(Window *win, char *cmd) {
    if (cmd[0] == 0)
        return;

    if (strcmp(cmd, "help") == 0) {
        window_print(win, "\nAvailable commands:\n - help\n - clear\n - shutdown\n - about");
    }
    else if (strcmp(cmd, "clear") == 0) {
        window_clear(win);
        return;
    }
    else if (strcmp(cmd, "shutdown") == 0) {
        window_print(win, "\nShutting down...");
        out_port_B(0x604, 0x2000);
        out_port_B(0xB004, 0x2000);
    } 
    else if (strcmp(cmd, "about") == 0) {
        window_print(win, "\nQuarkOS Cyclone\nMultitasking Kernel v0.3\n");
    }
    else {
        window_print(win, "\nUnknown command: ");
        window_print(win, cmd);
    }

    window_print(win, "\nquark|> ");
}