#include "user/bin/command.h"

int storage_pid = -1;
uint32_t current_dir_cluster = 0;
int current_disk = 0;

void tty_print(const char *text) {
    if (text)
        (void)write(STDOUT_FILENO, text, strlen(text));
}

void tty_clear(void) {
    tty_print("\033[2J\033[H");
}

int my_strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return *(const unsigned char *)a - *(const unsigned char *)b;
}

char scancode_to_char(uint8_t code, uint32_t mods) {
    bool sh = (mods & KEY_MOD_SHIFT) != 0;
    bool cap = (mods & KEY_MOD_CAPS) != 0;
    const char *r1 = "1234567890-=", *r1s = "!@#$%^&*()_+";
    const char *r2 = "qwertyuiop[]", *r2s = "QWERTYUIOP{}";
    const char *r3 = "asdfghjkl;'`", *r3s = "ASDFGHJKL:\"~";
    const char *r4 = "zxcvbnm,./", *r4s = "ZXCVBNM<>?";
    bool up = sh ^ cap;
    if (code >= 0x02 && code <= 0x0D)
        return sh ? r1s[code - 0x02] : r1[code - 0x02];
    if (code >= 0x10 && code <= 0x1B)
        return up ? r2s[code - 0x10] : r2[code - 0x10];
    if (code == 0x2B)
        return sh ? '|' : '\\';
    if (code >= 0x1E && code <= 0x29)
        return (code <= 0x26 ? up : sh) ? r3s[code - 0x1E] : r3[code - 0x1E];
    if (code >= 0x2C && code <= 0x35)
        return (code <= 0x32 ? up : sh) ? r4s[code - 0x2C] : r4[code - 0x2C];
    if (code == 0x39)
        return ' ';
    if (code == 0x1C)
        return '\n';
    if (code == 0x0E)
        return '\b';
    if (code == 0x0F)
        return '\t';
    return 0;
}

int resolve_target(const char *input, uint32_t *out_dir, char *out_name) {
    if (!input || !*input)
        return -1;
    char tmp[128];
    strncpy(tmp, input, 127);
    tmp[127] = 0;
    int len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = 0;
    if (!tmp[0] || (tmp[0] == '/' && !tmp[1]))
        return -1;
    if (strstr(tmp, "/")) {
        if (qk_path_resolve_ext(current_dir_cluster, tmp, out_dir, out_name) != 0)
            return -1;
        if (out_name)
            out_name[15] = 0;
    } else {
        if (out_dir)
            *out_dir = current_dir_cluster;
        if (out_name) {
            strncpy(out_name, tmp, 15);
            out_name[15] = 0;
        }
    }
    return 0;
}

void *read_file(const char *filename, uint32_t *out_size) {
    if (storage_pid < 0)
        return 0;
    uint32_t dc = current_dir_cluster;
    char rn[64];
    const char *tn = filename;
    message_t req, resp;
    memset(&req, 0, sizeof(req));
    req.data3 = (uint32_t)current_disk;
    if (filename[0] == '/') {
        req.type = MSG_FILE_OPEN;
        req.data1 = 0xFFFF;
        strncpy((char *)req.payload, filename, sizeof(req.payload) - 1);
        if (qk_ipc_send_recv(storage_pid, &req, &resp) != 0)
            return 0;
    } else {
        if (strstr(filename, "/")) {
            if (qk_path_resolve_ext(current_dir_cluster, filename, &dc, rn) != 0)
                return 0;
            tn = rn;
        }
        req.type = MSG_FILE_OPEN;
        req.data1 = dc;
        strncpy((char *)req.payload, tn, 12);
        if (qk_ipc_send_recv(storage_pid, &req, &resp) != 0)
            return 0;
    }
    if (resp.type != MSG_FILE_SIZE)
        return 0;
    uint32_t size = resp.data1;
    *out_size = size;
    uint8_t *buf = malloc(size);
    if (!buf)
        return 0;
    uint32_t off = 0;
    while (off < size) {
        memset(&req, 0, sizeof(req));
        req.type = MSG_REQ_DATA;
        req.data1 = off;
        req.data2 = sizeof(resp.payload);
        if (qk_ipc_send_recv(storage_pid, &req, &resp) != 0)
            break;
        if (resp.type != MSG_FILE_DATA)
            break;
        uint32_t got = resp.data1;
        if (!got)
            break;
        if (off + got > size)
            got = size - off;
        memcpy(buf + off, resp.payload, got);
        off += got;
    }
    return buf;
}

int save_file(const char *path, const char *data, uint32_t size) {
    uint32_t dc = current_dir_cluster;
    char en[64];
    if (resolve_target(path, &dc, en) != 0)
        return -1;
    return storage_send_file(storage_pid, dc, en, (const uint8_t *)data, size, 0);
}

int read_line_blocking(char *out, int max_len, uint8_t *special_sc) {
    if (!out || max_len < 1) return -1;
    if (special_sc) *special_sc = 0;
    int ptr = 0;
    while (ptr < max_len - 1) {
        char c;
        int n = read(STDIN_FILENO, &c, 1);
        if (n == 0) break;
        if (n < 0) return -1;
        if (c == '\n') break;
        out[ptr++] = c;
    }
    out[ptr] = 0;
    return 0;
}
int main(void) {
    storage_pid = qk_get_server_pid(QK_SERVER_STORAGE);
    qk_set_proc_name(-1, "command");
    message_t start;
    do { qk_ipc_recv(&start); } while (start.type != MSG_CLI_START);
    current_dir_cluster = start.data1;
    current_disk = (int)start.data2;
    qk_disk_select(current_disk);
    command_main((const char *)start.payload);
    return 0;
}
