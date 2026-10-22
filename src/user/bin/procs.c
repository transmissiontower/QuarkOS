#include "user/bin/command.h"

void command_main(const char *args) {
    (void)args;
    process_info_t info[32];
    int count = qk_get_procs(info, 32);
    tty_print(C_DIM "  PID   STATE      NAME\n");
    tty_print("  ---   --------   ----------------" C_RESET "\n");
    for (int i = 0; i < count; i++) {
        const char *st = "unknown ";
        const char *sc = C_RESET;
        if (info[i].state == 0) {
            st = "ready   ";
            sc = C_RESET;
        } else if (info[i].state == 1) {
            st = "running ";
            sc = C_GREEN;
        } else if (info[i].state == 2) {
            st = "blocked ";
            sc = C_YELLOW;
        } else if (info[i].state == 3) {
            st = "zombie  ";
            sc = C_RED;
        } else if (info[i].state == 4) {
            st = "wait_irq";
            sc = C_DIM;
        }
        char ps[8];
        int_to_str(info[i].pid, ps, 0);
        char pid3[4] = {' ', ' ', ' ', 0};
        int pl = strlen(ps);
        for (int j = 0; j < pl && j < 3; j++)
            pid3[3 - pl + j] = ps[j];
        tty_print("  ");
        tty_print(pid3);
        tty_print("   ");
        tty_print(sc);
        tty_print(st);
        tty_print(C_RESET);
        tty_print("   ");
        tty_print(info[i].name);
        tty_print("\n");
    }
}
