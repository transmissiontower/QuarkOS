#include "user/bin/command.h"

void command_main(const char *name) {
    if (!*name) {
        tty_print("quill: need a file name.\n");
        return;
    }
    uint32_t sz = 0;
    char *existing = read_file(name, &sz);
    char *buffer = malloc(NANO_BUFFER_SIZE);
    if (!buffer) {
        if (existing)
            free(existing);
        return;
    }
    buffer[0] = 0;
    if (existing && sz > 0) {
        uint32_t cp = sz < (uint32_t)(NANO_BUFFER_SIZE - 1) ? sz : (uint32_t)(NANO_BUFFER_SIZE - 1);
        memcpy(buffer, existing, cp);
        buffer[cp] = 0;
    }
    if (existing)
        free(existing);
    tty_print("quill: .save to save, .quit to exit\n");
    if (buffer[0]) {
        char lbuf[128];
        int ll = 0;
        for (int i = 0; buffer[i]; i++) {
            if (buffer[i] == '\n') {
                lbuf[ll] = 0;
                tty_print(lbuf);
                tty_print("\n");
                ll = 0;
            } else if (ll < 127)
                lbuf[ll++] = buffer[i];
        }
        if (ll) {
            lbuf[ll] = 0;
            tty_print(lbuf);
        }
    }
    char line[128];
    while (1) {
        tty_print("[quill] ");
        uint8_t sp = 0;
        int r = read_line_blocking(line, sizeof(line), &sp);
        if (r == 2 && sp == SPECIAL_CTRL_C) {
            tty_print("\nquill: exit.\n");
            break;
        }
        if (r == 1) {
            tty_print("\n");
            continue;
        }
        if (my_strcmp(line, ".save") == 0 || my_strcmp(line, ".safe") == 0) {
            if (save_file(name, buffer, strlen(buffer)) == 0)
                tty_print("quill: saved.\n");
            else
                tty_print("quill: save failed.\n");
        } else if (my_strcmp(line, ".quit") == 0) {
            break;
        } else if (my_strcmp(line, ".clear") == 0) {
            buffer[0] = 0;
        } else {
            int bl = strlen(buffer), ll2 = strlen(line);
            if (bl + ll2 + 2 < NANO_BUFFER_SIZE) {
                strcat(buffer, line);
                strcat(buffer, "\n");
            }
        }
    }
    free(buffer);
}
