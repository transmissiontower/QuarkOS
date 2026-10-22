#include "user/bin/command.h"

void command_main(const char *fn) {
    if (!*fn || my_strcmp(fn, "-") == 0) {
        char buf[256];
        int n;
        while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0)
            if (write(STDOUT_FILENO, buf, (unsigned int)n) < 0) return;
        return;
    }
    uint32_t sz = 0;
    char *data = read_file(fn, &sz);
    if (!data) {
        tty_print("cat: not found.\n");
        return;
    }
    if (sz) write(STDOUT_FILENO, data, sz);
    free(data);
}
