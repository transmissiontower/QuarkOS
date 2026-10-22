#include "user/bin/command.h"

void command_main(const char *fn) {
    if (!*fn) {
        tty_print("hex: need a filename.\n");
        return;
    }
    uint32_t sz = 0;
    uint8_t *data = read_file(fn, &sz);
    if (!data || !sz) {
        tty_print("hex: not found.\n");
        if (data)
            free(data);
        return;
    }
    const char *hc = "0123456789ABCDEF";
    for (uint32_t off = 0; off < sz; off += 16) {
        char line[80];
        int li = 0;
        char hx[16];
        uint_to_hex(off, hx);
        int hl = strlen(hx);
        for (int k = 0; k < hl; k++)
            line[li++] = hx[k];
        line[li++] = ':';
        line[li++] = ' ';
        for (int i = 0; i < 16; i++) {
            if (off + i < sz) {
                uint8_t b = data[off + i];
                line[li++] = hc[b >> 4];
                line[li++] = hc[b & 0xF];
                line[li++] = ' ';
            } else {
                line[li++] = ' ';
                line[li++] = ' ';
                line[li++] = ' ';
            }
        }
        line[li++] = '|';
        for (int i = 0; i < 16 && off + i < sz; i++) {
            uint8_t b = data[off + i];
            line[li++] = (b >= 32 && b <= 126) ? (char)b : '.';
        }
        line[li++] = '|';
        line[li] = 0;
        tty_print(line);
        tty_print("\n");
    }
    free(data);
}
