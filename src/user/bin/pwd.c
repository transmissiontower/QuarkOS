#include "user/bin/command.h"

void command_main(const char *args) {
    (void)args;
    char path[128];
    if (qk_getenv("PWD", path, sizeof(path)) == 0) {
        write(STDOUT_FILENO, path, strlen(path));
        write(STDOUT_FILENO, "\n", 1);
    } else {
        write(STDOUT_FILENO, "/\n", 2);
    }
}
