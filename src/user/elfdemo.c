#include "user/lib/libapi.h"

static void print_status_line(int row, const char *label, uint32_t color) {
    qk_print_at(40, 40 + row * 18, color, label);
}

int main(void) {
    qk_set_proc_name(-1, "elfdemo");
    print_status_line(0, "ELF demo: loader test", 0xFF8CCF4D);
    for (int i = 0; i < 3; i++) {
        print_status_line(1 + i, "running...", 0xFFCCCCCC);
        qk_sleep(250);
    }
    print_status_line(4, "done.", 0xFF61AFEF);
    return 0;
}
