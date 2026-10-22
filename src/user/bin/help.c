#include "user/bin/command.h"

void command_main(const char *args) {
    (void)args;
    tty_print(C_CYAN "  Files  " C_RESET
                     "  ls  cat <f>  hex <f>  touch <f>  mkdir <d>\n");
    tty_print(C_CYAN "  Shell  " C_RESET "  cd <path>  pwd  setenv KEY=VALUE  exit\n");
    tty_print(C_CYAN "  Text   " C_RESET "  echo <text>\n");
    tty_print(C_CYAN "  Editor " C_RESET "  quill <f>\n");
    tty_print(C_CYAN "  System " C_RESET "  procs  kill <pid>  exec <path>  uptime  clear\n");
    tty_print(C_CYAN "  Env    " C_RESET "  env  setenv K=V  getenv K\n");
}
