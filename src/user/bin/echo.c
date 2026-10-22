#include "user/bin/command.h"

void command_main(const char *args) {
    if (!args) args = "";
    write(STDOUT_FILENO, args, strlen(args));
    write(STDOUT_FILENO, "\n", 1);
}
