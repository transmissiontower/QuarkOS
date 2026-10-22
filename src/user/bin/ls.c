#include "user/bin/command.h"

void command_main(const char *args) {
    (void)args;
    if (current_disk < 0) {
        int ndisks = qk_disk_count();
        for (int i = 0; i < ndisks; i++) {
            disk_info_t info;
            if (qk_disk_info(i, &info) != 0)
                continue;
            const char *ts = info.type == 1   ? "hdd"
                             : info.type == 2 ? "cdrom"
                             : info.type == 3 ? "floppy"
                             : info.type == 4 ? "usb"
                                              : "disk";
            tty_print("  " C_CYAN "d" C_RESET "  sd");
            char idx_c[4];
            idx_c[0] = '0' + (char)i;
            idx_c[1] = 0;
            tty_print(idx_c);
            tty_print("  " C_DIM);
            tty_print(ts);
            uint32_t mb = info.capacity_lo / 2048;
            char num[16];
            int ni = 0;
            uint32_t val = mb;
            const char *unit = " MB";
            if (val >= 1024) {
                val /= 1024;
                unit = " GB";
            }
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
            tty_print(" ");
            num[ni] = 0;
            tty_print(num);
            tty_print(unit);
            tty_print(C_RESET "\n");
        }
        return;
    }
    message_t req;
    memset(&req, 0, sizeof(req));
    req.type = MSG_DIR_REQ;
    req.data1 = current_dir_cluster;
    req.data3 = (uint32_t)current_disk;
    qk_sys_ipc_send(storage_pid, &req);
    while (1) {
        message_t resp;
        qk_ipc_recv(&resp);
        if (resp.sender_pid != storage_pid || resp.type != MSG_DIR_REPLY)
            continue;
        char *ptr = (char *)resp.payload;
        while (*ptr) {
            char line[80];
            int li = 0;
            while (*ptr && *ptr != '\n' && li < 79)
                line[li++] = *ptr++;
            line[li] = 0;
            if (*ptr == '\n')
                ptr++;
            if (!li)
                continue;
            int is_dir = (line[0] == '1');
            const char *name = line;
            while (*name && *name != '|')
                name++;
            if (*name == '|')
                name++;
            while (*name && *name != '|')
                name++;
            if (*name == '|')
                name++;

            char lname[64];
            int li2 = 0;
            for (const char *s = name; *s && li2 < 63; s++)
                lname[li2++] = (*s >= 'A' && *s <= 'Z') ? *s + 32 : *s;
            lname[li2] = 0;
            if (is_dir)
                tty_print("  " C_CYAN "d" C_RESET "  ");
            else
                tty_print(C_DIM "  -  " C_RESET);
            tty_print(lname);
            tty_print("\n");
        }
        if (resp.data1 == 0)
            break;
    }
}
