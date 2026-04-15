#include "user/lib/path.h"
#include "lib/stdbool.h"
#include "lib/stdlib.h"
#include "user/lib/libapi.h"
typedef struct {
    int is_dir;
    uint16_t cluster;
    char name[16];
} dir_entry_info_t;
static int upcase(char c)
{
    if (c >= 'a' && c <= 'z')
        return c - 32;
    return c;
}
static int name_equals(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *b) {
        if (upcase(*a) != upcase(*b))
            return 0;
        a++;
        b++;
    }
    return (*a == 0 && *b == 0);
}
static int dir_lookup(int storage_pid, uint16_t dir_cluster, const char *target,
                      dir_entry_info_t *out_entry)
{
    if (!target || !*target)
        return -1;
    message_t req;
    memset(&req, 0, sizeof(req));
    req.type = MSG_DIR_REQ;
    req.data1 = dir_cluster;
    qk_sys_ipc_send(storage_pid, &req);
    int found = 0;
    while (1) {
        message_t resp;
        qk_ipc_recv(&resp);
        if (resp.sender_pid != storage_pid || resp.type != MSG_DIR_REPLY)
            continue;
        char *ptr = (char *)resp.payload;
        while (*ptr) {
            dir_entry_info_t entry;
            entry.is_dir = (*ptr == '1') ? 1 : 0;
            while (*ptr && *ptr != '|')
                ptr++;
            if (*ptr == '|')
                ptr++;
            int cluster = 0;
            while (*ptr >= '0' && *ptr <= '9') {
                cluster = cluster * 10 + (*ptr - '0');
                ptr++;
            }
            entry.cluster = (uint16_t)cluster;
            if (*ptr == '|')
                ptr++;
            int ni = 0;
            while (*ptr && *ptr != '\n' && ni < 15) {
                entry.name[ni++] = *ptr++;
            }
            entry.name[ni] = 0;
            if (*ptr == '\n')
                ptr++;
            if (!found && name_equals(entry.name, target)) {
                if (out_entry)
                    *out_entry = entry;
                found = 1;
            }
        }
        if (resp.data1 == 0)
            break;
    }
    return found ? 0 : -1;
}
int qk_path_resolve_ext(uint16_t start_cluster, const char *path, uint16_t *out_dir_cluster,
                        char *out_name)
{
    int storage_pid = qk_get_server_pid(QK_SERVER_STORAGE);
    if (storage_pid < 0)
        return -1;
    uint16_t current_cluster = start_cluster;
    char segment[16];
    int si = 0;
    const char *p = path;
    if (*p == '/') {
        current_cluster = 0;
        while (*p == '/')
            p++;
    }
    if (!*p) {
        if (out_dir_cluster)
            *out_dir_cluster = 0;
        if (out_name)
            out_name[0] = 0;
        return 0;
    }
    while (*p) {
        si = 0;
        while (*p && *p != '/') {
            if (si < 15)
                segment[si++] = *p;
            p++;
        }
        segment[si] = 0;
        if (*p == '/') {
            dir_entry_info_t entry;
            if (dir_lookup(storage_pid, current_cluster, segment, &entry) != 0 || !entry.is_dir) {
                return -1;
            }
            current_cluster = entry.cluster;
            while (*p == '/')
                p++;
        } else {
            if (out_dir_cluster)
                *out_dir_cluster = current_cluster;
            if (out_name)
                strncpy(out_name, segment, 15);
            return 0;
        }
    }
    return -1;
}
int qk_path_resolve(const char *path, uint16_t *out_dir_cluster, char *out_name)
{
    return qk_path_resolve_ext(0, path, out_dir_cluster, out_name);
}
