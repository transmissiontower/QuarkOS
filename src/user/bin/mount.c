#include "user/bin/command.h"

void command_main(const char *args) {
    int ndisks = qk_disk_count();
    if (!args || !*args) {
        for (int i = 0; i < ndisks; i++) {
            disk_info_t info;
            if (qk_disk_info(i, &info) != 0)
                continue;
            uint32_t mb = info.capacity_lo / 2048;
            char line[80];
            int p = 0;
            line[p++] = ' ';
            line[p++] = ' ';
            line[p++] = 'C' + (char)i;
            line[p++] = ':';
            line[p++] = ' ';
            const char *ts = info.type == 1   ? "HDD"
                             : info.type == 2 ? "CD-ROM"
                             : info.type == 3 ? "Floppy"
                             : info.type == 4 ? "USB"
                                              : "Disk";
            for (int j = 0; ts[j] && p < 60; j++)
                line[p++] = ts[j];
            line[p++] = ' ';

            uint32_t val = mb;
            const char *unit = "MB";
            if (val >= 1024) {
                val /= 1024;
                unit = "GB";
            }
            char num[12];
            int ni = 0;
            if (val == 0)
                num[ni++] = '0';
            else {
                uint32_t t = val;
                while (t) {
                    num[ni++] = '0' + (t % 10);
                    t /= 10;
                }
                for (int a = 0; a < ni / 2; a++) {
                    char c = num[a];
                    num[a] = num[ni - 1 - a];
                    num[ni - 1 - a] = c;
                }
            }
            for (int j = 0; j < ni && p < 70; j++)
                line[p++] = num[j];
            line[p++] = ' ';
            for (int j = 0; unit[j] && p < 74; j++)
                line[p++] = unit[j];
            line[p++] = '\n';
            line[p] = 0;
            tty_print(line);
        }
        return;
    }

    int idx = -1;
    if (args[0] == 's' && args[1] == 'd' && args[2] >= '0' && args[2] <= '9')
        idx = args[2] - '0';
    else if (args[0] >= '0' && args[0] <= '9')
        idx = args[0] - '0';
    if (idx < 0 || idx >= ndisks) {
        tty_print("  mnt: invalid. use sd0..sd9\n");
        return;
    }
    message_t m, r;
    memset(&m, 0, sizeof(m));
    m.type = MSG_DISK_MOUNT;
    m.data1 = (uint32_t)idx;
    qk_ipc_send_recv(storage_pid, &m, &r);
    if (r.type == MSG_OK) {
        tty_print("  sd");
        char c[2] = {'0' + idx, 0};
        tty_print(c);
        tty_print(" mounted\n");
    } else {
        tty_print("  mnt: failed.\n");
    }
}
