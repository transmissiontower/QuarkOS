#include "kernel/ipc.h"
#include "lib/stdbool.h"
#include "lib/stdlib.h"
#include "user/lib/libapi.h"
#include "user/lib/path.h"
#include "user/lib/storage_client.h"
#include "user/lib/window.h"
#include <stddef.h>

static int storage_pid = -1;
static int comp_pid = -1;
static uint16_t current_dir_cluster = 0;
static char current_path[128] = "/";

#define MAX_PATH_DEPTH 16
#define NANO_BUFFER_SIZE 4096
#define QUILL_SCROLL_LINES 12
#define SCANCODE_ARROW_UP 0x48
#define SCANCODE_ARROW_DOWN 0x50
#define SCANCODE_CTRL 0x1D
#define SPECIAL_CTRL_C 0x03
static uint16_t dir_stack[MAX_PATH_DEPTH] = {0};
static char dir_names[MAX_PATH_DEPTH][16];
static int dir_depth = 0;

typedef struct {
    int is_dir;
    uint16_t cluster;
    char name[16];
} dir_entry_info_t;

static int ensure_server_pids(void)
{
    if (storage_pid < 0)
        storage_pid = qk_get_server_pid(QK_SERVER_STORAGE);
    if (comp_pid < 0)
        comp_pid = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    if (storage_pid < 0 || comp_pid < 0)
        return -1;
    return 0;
}

int my_strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int str_starts_with(const char *str, const char *prefix)
{
    while (*prefix) {
        if (*prefix++ != *str++)
            return 0;
    }
    return 1;
}

static int cmd_is(const char *input, const char *primary, const char *alias)
{
    if (my_strcmp(input, primary) == 0)
        return 1;
    if (alias && alias[0] && my_strcmp(input, alias) == 0)
        return 1;
    return 0;
}

static const char *cmd_arg_ptr(const char *input, const char *primary, const char *alias)
{
    uint32_t len = strlen(primary);
    if (str_starts_with(input, primary) && input[len] == ' ')
        return input + len + 1;
    if (alias && alias[0]) {
        len = strlen(alias);
        if (str_starts_with(input, alias) && input[len] == ' ')
            return input + len + 1;
    }
    return 0;
}

#define KEY_MOD_SHIFT 0x01
#define KEY_MOD_CTRL  0x02
#define KEY_MOD_ALT   0x04
#define KEY_MOD_CAPS  0x08

char scancode_to_char(uint8_t code, uint32_t mods)
{
    bool shifted = (mods & KEY_MOD_SHIFT) != 0;
    bool caps    = (mods & KEY_MOD_CAPS)  != 0;

    
    const char *row1    = "1234567890-=";
    const char *row1_sh = "!@#$%^&*()_+";
    const char *row2    = "qwertyuiop[]";
    const char *row2_sh = "QWERTYUIOP{}";
    const char *row3    = "asdfghjkl;'`";
    const char *row3_sh = "ASDFGHJKL:\"~";
    const char *row4    = "zxcvbnm,./";
    const char *row4_sh = "ZXCVBNM<>?";

    if (code >= 0x02 && code <= 0x0D)
        return shifted ? row1_sh[code - 0x02] : row1[code - 0x02];

    if (code >= 0x10 && code <= 0x1B) {
        bool upper = shifted ^ caps;
        return upper ? row2_sh[code - 0x10] : row2[code - 0x10];
    }
    if (code >= 0x1E && code <= 0x29) {
        
        if (code <= 0x26) { 
            bool upper = shifted ^ caps;
            return upper ? row3_sh[code - 0x1E] : row3[code - 0x1E];
        }
        return shifted ? row3_sh[code - 0x1E] : row3[code - 0x1E];
    }
    if (code >= 0x2C && code <= 0x35) {
        if (code <= 0x32) { 
            bool upper = shifted ^ caps;
            return upper ? row4_sh[code - 0x2C] : row4[code - 0x2C];
        }
        return shifted ? row4_sh[code - 0x2C] : row4[code - 0x2C];
    }
    if (code == 0x39) return ' ';
    if (code == 0x1C) return '\n';
    if (code == 0x0E) return '\b';
    if (code == 0x0F) return '\t';
    if (code == 0x35) return shifted ? '?' : '/';
    return 0;
}

void shell_print(const char *text)
{
    message_t msg;
    msg.type = MSG_WIN_PRINT;
    int len = strlen(text);
    if (len > 127)
        len = 127;
    strncpy((char *)msg.payload, text, len);
    msg.payload[len] = 0;
    qk_sys_ipc_send(comp_pid, &msg);
}

static void shell_print_long(const char *text)
{
    if (!text)
        return;
    const char *ptr = text;
    while (*ptr) {
        char chunk[120];
        int idx = 0;
        while (*ptr && idx < (int)sizeof(chunk) - 1) {
            chunk[idx++] = *ptr++;
        }
        chunk[idx] = 0;
        if (idx == 0)
            break;
        shell_print(chunk);
    }
}

static void shell_print_range(const char *start, const char *end)
{
    if (!start || !end || start >= end)
        return;
    const char *ptr = start;
    char chunk[120];
    while (ptr < end) {
        int idx = 0;
        while (ptr < end && idx < (int)sizeof(chunk) - 1) {
            chunk[idx++] = *ptr++;
        }
        chunk[idx] = 0;
        if (idx == 0)
            break;
        shell_print(chunk);
    }
}

static void shell_trigger_page_fault(void)
{
    shell_print("Triggering page fault (press R to reboot)...\n");
    volatile uint8_t *bad = (volatile uint8_t *)0xFFFFFFFF;
    *bad = 0xAB;
    shell_print("Failed to trigger page fault.\n");
}

static void shell_trigger_kernel_panic(void)
{
    shell_print("Triggering kernel panic (press R to reboot)...\n");
    qk_trigger_panic("Shell requested kernel panic");
    shell_print("Kernel panic syscall returned unexpectedly.\n");
}

static int quill_count_lines(const char *text)
{
    if (!text || !*text)
        return 0;
    int lines = 1;
    for (const char *ptr = text; *ptr; ptr++) {
        if (*ptr == '\n')
            lines++;
    }
    return lines;
}

static void quill_show_scroll_window(const char *buffer, int scroll_offset_lines, int window_lines)
{
    if (!buffer || !*buffer) {
        shell_print("quill: buffer empty.\n");
        return;
    }
    if (window_lines <= 0)
        window_lines = 1;
    int total_lines = quill_count_lines(buffer);
    int display_lines = (total_lines < window_lines) ? total_lines : window_lines;
    if (display_lines <= 0)
        display_lines = total_lines;
    if (display_lines <= 0) {
        shell_print("quill: buffer empty.\n");
        return;
    }
    int max_offset = (total_lines > display_lines) ? (total_lines - display_lines) : 0;
    if (scroll_offset_lines > max_offset)
        scroll_offset_lines = max_offset;
    if (scroll_offset_lines < 0)
        scroll_offset_lines = 0;
    int start_line = total_lines - display_lines - scroll_offset_lines;
    if (start_line < 0)
        start_line = 0;

    const char *start_ptr = buffer;
    int current_line = 0;
    while (*start_ptr && current_line < start_line) {
        if (*start_ptr == '\n')
            current_line++;
        start_ptr++;
    }

    const char *end_ptr = start_ptr;
    int lines_collected = 0;
    while (*end_ptr && lines_collected < display_lines) {
        if (*end_ptr == '\n') {
            lines_collected++;
            end_ptr++;
            if (lines_collected == display_lines)
                break;
        } else {
            end_ptr++;
        }
    }
    if (!*end_ptr && lines_collected < display_lines) {
        lines_collected = display_lines;
    }

    shell_print("---- quill view ");
    char num_buf[16];
    int_to_str(start_line + 1, num_buf, 0);
    shell_print(num_buf);
    shell_print("-");
    int_to_str(start_line + lines_collected, num_buf, 0);
    shell_print(num_buf);
    shell_print(" of ");
    int_to_str(total_lines, num_buf, 0);
    shell_print(num_buf);
    shell_print(" ----\n");
    if (end_ptr > start_ptr) {
        shell_print_range(start_ptr, end_ptr);
        if (*(end_ptr - 1) != '\n')
            shell_print("\n");
    }
    shell_print("-----------------------\n");
}

void shell_clear()
{
    message_t msg;
    msg.type = MSG_WIN_CLEAR;
    qk_sys_ipc_send(comp_pid, &msg);
}

void shell_show_prompt()
{
    shell_print(current_path);
    shell_print(" $ ");
}

static char upcase(char c)
{
    if (c >= 'a' && c <= 'z')
        return c - 32;
    return c;
}

static uint32_t hex_to_uint(const char *s)
{
    uint32_t val = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    while (*s) {
        uint32_t v = 0;
        if (*s >= '0' && *s <= '9')
            v = *s - '0';
        else if (*s >= 'a' && *s <= 'f')
            v = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F')
            v = *s - 'A' + 10;
        else
            break;
        val = (val << 4) | v;
        s++;
    }
    return val;
}

static int shell_name_equals(const char *a, const char *b)
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

static void shell_rebuild_path()
{
    if (dir_depth == 0) {
        current_path[0] = '/';
        current_path[1] = 0;
        return;
    }
    current_path[0] = 0;
    int pos = 0;
    for (int i = 1; i <= dir_depth; i++) {
        if (pos < (int)sizeof(current_path) - 1) {
            current_path[pos++] = '/';
        }
        const char *name = dir_names[i];
        for (int j = 0; name[j] && pos < (int)sizeof(current_path) - 1; j++) {
            current_path[pos++] = name[j];
        }
    }
    current_path[pos] = 0;
}

static int shell_dir_lookup(uint16_t dir_cluster, const char *target, dir_entry_info_t *out_entry)
{
    if (!target || !*target)
        return -1;
    message_t req;
    memset(&req, 0, sizeof(req));
    req.type = MSG_DIR_REQ;
    req.data1 = dir_cluster;
    qk_sys_ipc_send(storage_pid, &req);

    int found = 0;
    dir_entry_info_t result;
    while (1) {
        message_t resp;
        qk_ipc_recv(&resp);
        if (resp.sender_pid != storage_pid || resp.type != MSG_DIR_REPLY)
            continue;
        char *ptr = (char *)resp.payload;
        while (*ptr) {
            dir_entry_info_t entry;
            entry.is_dir = (*ptr == '1') ? 1 : 0;
            while (*ptr && *ptr != '|') {
                ptr++;
            }
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
            if (!found && shell_name_equals(entry.name, target)) {
                result = entry;
                found = 1;
            }
        }
        if (resp.data1 == 0)
            break;
    }
    if (found && out_entry)
        *out_entry = result;
    return found ? 0 : -1;
}

static int shell_resolve_target(const char *input, uint16_t *out_dir_cluster, char *out_name)
{
    if (!input || !*input)
        return -1;
    char temp[128];
    strncpy(temp, input, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = 0;

    int len = strlen(temp);
    while (len > 1 && temp[len - 1] == '/') {
        temp[len - 1] = 0;
        len--;
    }
    if (temp[0] == 0)
        return -1;
    if (temp[0] == '/' && temp[1] == 0)
        return -1;

    if (strstr(temp, "/")) {
        if (qk_path_resolve_ext(current_dir_cluster, temp, out_dir_cluster, out_name) != 0)
            return -1;
        if (out_name)
            out_name[15] = 0;
    } else {
        if (out_dir_cluster)
            *out_dir_cluster = current_dir_cluster;
        if (out_name) {
            strncpy(out_name, temp, 15);
            out_name[15] = 0;
        }
    }
    return 0;
}

static int shell_read_line_blocking(char *out, int max_len, uint8_t *special_scancode)
{
    int ptr = 0;
    if (max_len <= 0)
        return 0;
    out[0] = 0;
    if (special_scancode)
        *special_scancode = 0;
    uint8_t awaiting_extended = 0;
    int ctrl_down = 0;
    while (1) {
        message_t msg;
        qk_ipc_recv(&msg);
        if (msg.type != MSG_KEY_EVENT)
            continue;
        uint32_t raw = msg.data1;
        uint8_t is_release = (raw & 0x80) != 0;
        uint8_t code = (uint8_t)(raw & 0x7F);
        int extended = (raw & 0x100) != 0;
        
        if (!extended && code == 0x00) {
            awaiting_extended = 1;
            continue;
        }
        if (is_release) {
            if (code == SCANCODE_CTRL)
                ctrl_down = 0;
            awaiting_extended = 0;
            continue;
        }
        if (code == SCANCODE_CTRL) {
            ctrl_down = 1;
            continue;
        }
        if (awaiting_extended) {
            uint8_t keycode = code;
            awaiting_extended = 0;
            if (keycode == SCANCODE_ARROW_UP || keycode == SCANCODE_ARROW_DOWN) {
                if (special_scancode)
                    *special_scancode = keycode;
                return 1;
            }
        }
        if (code == SCANCODE_ARROW_UP || code == SCANCODE_ARROW_DOWN) {
            if (special_scancode)
                *special_scancode = code;
            return 1;
        }
        if (ctrl_down && code == 0x2E) {
            if (special_scancode)
                *special_scancode = SPECIAL_CTRL_C;
            return 2;
        }
        char c = scancode_to_char(code, msg.data2);
        if (c == '\n') {
            shell_print("\n");
            out[ptr] = 0;
            return 0;
        }
        if (c == '\b') {
            if (ptr > 0) {
                ptr--;
                out[ptr] = 0;
                shell_print("\b");
            }
            continue;
        }
        if (c && ptr < max_len - 1) {
            out[ptr++] = c;
            out[ptr] = 0;
            char tmp[2] = {c, 0};
            shell_print(tmp);
        }
    }
}

static void shell_procs()
{
    process_info_t info[32];
    int count = qk_get_procs(info, 32);
    shell_print("PID  STATE    NAME\n");
    shell_print("---  -------  ----------------\n");
    for (int i = 0; i < count; i++) {
        char pid_str[16];
        uint_to_hex(info[i].pid, pid_str);

        const char *state_str = "UNKNOWN";
        if (info[i].state == 0)
            state_str = "READY";
        else if (info[i].state == 1)
            state_str = "RUNNING";
        else if (info[i].state == 2)
            state_str = "BLOCKED";
        else if (info[i].state == 3)
            state_str = "ZOMBIE";
        else if (info[i].state == 4)
            state_str = "WAIT_IRQ";

        char line[128];
        char pid_val[16];
        int_to_str(info[i].pid, pid_val, 0);

        line[0] = 0;
        strcat(line, pid_val);
        strcat(line, "    ");
        strcat(line, state_str);
        strcat(line, "    ");
        strcat(line, info[i].name);
        strcat(line, "\n");
        shell_print(line);
    }
}

void *shell_read_file(const char *filename, uint32_t *out_size)
{
    if (storage_pid < 0)
        return 0;
    uint16_t dir_cluster = current_dir_cluster;
    char resolved_name[16];
    const char *target_name = filename;

    if (strstr(filename, "/")) {
        if (qk_path_resolve_ext(current_dir_cluster, filename, &dir_cluster, resolved_name) != 0) {
            return 0;
        }
        target_name = resolved_name;
    }

    message_t req;
    memset(&req, 0, sizeof(req));
    req.type = MSG_FILE_OPEN;
    req.data1 = dir_cluster;
    strncpy((char *)req.payload, target_name, 12);
    qk_sys_ipc_send(storage_pid, &req);

    message_t resp;
    while (1) {
        qk_ipc_recv(&resp);
        if (resp.sender_pid == storage_pid)
            break;
    }

    if (resp.type != MSG_FILE_SIZE) {
        return 0;
    }

    uint32_t size = resp.data1;
    *out_size = size;
    uint8_t *buffer = malloc(size);
    if (!buffer)
        return 0;

    uint32_t offset = 0;
    while (offset < size) {
        memset(&req, 0, sizeof(req));
        req.type = MSG_REQ_DATA;
        req.data1 = offset;
        req.data2 = 100;
        qk_sys_ipc_send(storage_pid, &req);

        while (1) {
            qk_ipc_recv(&resp);
            if (resp.sender_pid == storage_pid && resp.type == MSG_FILE_DATA)
                break;
        }

        uint32_t bytes_got = resp.data1;
        if (bytes_got == 0)
            break;

        memcpy(buffer + offset, resp.payload, bytes_got);
        offset += bytes_got;
    }

    return buffer;
}

void shell_cat(const char *filename)
{
    uint32_t size = 0;
    char *data = (char *)shell_read_file(filename, &size);

    if (data && size > 0) {
        char pbuf[100];
        uint32_t p = 0;
        while (p < size) {
            int chunk = 0;
            for (int k = 0; k < 99 && p + k < size; k++) {
                pbuf[k] = data[p + k];
                chunk++;
            }
            pbuf[chunk] = 0;
            shell_print(pbuf);
            p += chunk;
        }
        shell_print("\n");
        free(data);
    } else {
        shell_print("Error: File not found.\n");
    }
}

void shell_hex(const char *filename)
{
    uint32_t size = 0;
    uint8_t *data = (uint8_t *)shell_read_file(filename, &size);
    if (!data || size == 0) {
        shell_print("Error: File not found or empty.\n");
        if (data)
            free(data);
        return;
    }

    const char *hexchars = "0123456789ABCDEF";
    for (uint32_t offset = 0; offset < size; offset += 16) {
        char off_buf[16];
        uint_to_hex(offset, off_buf);
        shell_print(off_buf);
        shell_print(": ");

        char hex_chunk[16 * 3 + 1];
        int hex_idx = 0;
        for (int i = 0; i < 16; i++) {
            if (offset + i < size) {
                uint8_t byte = data[offset + i];
                hex_chunk[hex_idx++] = hexchars[(byte >> 4) & 0xF];
                hex_chunk[hex_idx++] = hexchars[byte & 0xF];
                hex_chunk[hex_idx++] = ' ';
            } else {
                hex_chunk[hex_idx++] = ' ';
                hex_chunk[hex_idx++] = ' ';
                hex_chunk[hex_idx++] = ' ';
            }
        }
        hex_chunk[hex_idx] = 0;
        shell_print(hex_chunk);

        shell_print("|");
        for (int i = 0; i < 16 && (offset + i) < size; i++) {
            uint8_t byte = data[offset + i];
            char ascii = (byte >= 32 && byte <= 126) ? (char)byte : '.';
            char tmp[2] = {ascii, 0};
            shell_print(tmp);
        }
        shell_print("|\n");
    }
    free(data);
}

int shell_save_file(const char *path, const char *data, uint32_t size)
{
    if (!path || !*path)
        return -1;
    uint16_t dir_cluster = current_dir_cluster;
    char entry_name[16];
    if (shell_resolve_target(path, &dir_cluster, entry_name) != 0)
        return -1;
    const uint8_t *bytes = (const uint8_t *)data;
    return storage_send_file(storage_pid, dir_cluster, entry_name, bytes, size);
}

void shell_ls()
{
    message_t req;
    memset(&req, 0, sizeof(req));
    req.type = MSG_DIR_REQ;
    req.data1 = current_dir_cluster;
    shell_print("Glimpsing ");
    shell_print(current_path);
    shell_print("\n");
    qk_sys_ipc_send(storage_pid, &req);

    message_t resp;
    while (1) {
        qk_ipc_recv(&resp);
        if (resp.sender_pid == storage_pid && resp.type == MSG_DIR_REPLY) {
            char *ptr = (char *)resp.payload;
            while (*ptr) {
                char line[64];
                int li = 0;
                while (*ptr && *ptr != '\n' && li < 63)
                    line[li++] = *ptr++;
                line[li] = 0;
                if (*ptr == '\n')
                    ptr++;
                if (li > 0) {
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
                    if (is_dir)
                        shell_print("[dir] ");
                    else
                        shell_print("      ");
                    shell_print(name);
                    shell_print("\n");
                }
            }
            if (resp.data1 == 0)
                break;
        }
    }
}

void shell_touch(const char *path)
{
    if (!path || !*path) {
        shell_print("touch: specify a file name.\n");
        return;
    }
    uint16_t dir_cluster = current_dir_cluster;
    char entry_name[16];
    if (shell_resolve_target(path, &dir_cluster, entry_name) != 0) {
        shell_print("touch: invalid path.\n");
        return;
    }
    if (storage_send_file(storage_pid, dir_cluster, entry_name, 0, 0) == 0)
        shell_print("touch: done.\n");
    else
        shell_print("touch: failed.\n");
}

void shell_mkdir(const char *path)
{
    if (!path || !*path) {
        shell_print("mkdir: specify a directory name.\n");
        return;
    }
    uint16_t dir_cluster = current_dir_cluster;
    char entry_name[16];
    if (shell_resolve_target(path, &dir_cluster, entry_name) != 0) {
        shell_print("mkdir: invalid path.\n");
        return;
    }
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_DIR_CREATE;
    msg.data1 = dir_cluster;
    strncpy((char *)msg.payload, entry_name, 63);
    msg.payload[63] = 0;
    qk_sys_ipc_send(storage_pid, &msg);
    message_t resp;
    while (1) {
        qk_ipc_recv(&resp);
        if (resp.sender_pid == storage_pid)
            break;
    }
    if (resp.type == MSG_OK)
        shell_print("mkdir: directory created.\n");
    else
        shell_print("mkdir: failed.\n");
}

void shell_cd(const char *target)
{
    if (!target || !*target || my_strcmp(target, "/") == 0) {
        dir_depth = 0;
        dir_stack[0] = 0;
        current_dir_cluster = 0;
        shell_rebuild_path();
        return;
    }
    if (my_strcmp(target, "..") == 0) {
        if (dir_depth > 0)
            dir_depth--;
        current_dir_cluster = dir_stack[dir_depth];
        shell_rebuild_path();
        return;
    }
    dir_entry_info_t entry;
    if (shell_dir_lookup(current_dir_cluster, target, &entry) != 0 || !entry.is_dir) {
        shell_print("drift: directory not found.\n");
        return;
    }
    dir_depth++;
    dir_stack[dir_depth] = entry.cluster;
    strncpy(dir_names[dir_depth], entry.name, 15);
    current_dir_cluster = entry.cluster;
    shell_rebuild_path();
}

void shell_nano(const char *name)
{
    if (!name || !*name) {
        shell_print("quill: specify a file name.\n");
        return;
    }
    uint32_t size = 0;
    char *existing = (char *)shell_read_file(name, &size);
    char *buffer = (char *)malloc(NANO_BUFFER_SIZE);
    if (!buffer) {
        if (existing)
            free(existing);
        return;
    }
    buffer[0] = 0;
    if (existing && size > 0) {
        uint32_t copy = (size < NANO_BUFFER_SIZE - 1) ? size : (NANO_BUFFER_SIZE - 1);
        memcpy(buffer, existing, copy);
        buffer[copy] = 0;
    }
    if (existing)
        free(existing);

    shell_print("Entering quill mode. Commands: .save/.safe, .quit, .clear\n");

    uint32_t initial_len = strlen(buffer);
    if (initial_len > 0) {
        shell_print_long(buffer);
        if (buffer[initial_len - 1] != '\n')
            shell_print("\n");
    }
    int quill_scroll_offset = 0;
    const int quill_scroll_window = QUILL_SCROLL_LINES;
    char line[128];
    while (1) {
        shell_print("[quill] ");
        uint8_t special_key = 0;
        int input_type = shell_read_line_blocking(line, sizeof(line), &special_key);
        if (input_type == 1) {
            shell_print("\n");
            int total_lines = quill_count_lines(buffer);
            int display_lines =
                (total_lines < quill_scroll_window) ? total_lines : quill_scroll_window;
            if (display_lines <= 0)
                display_lines = quill_scroll_window;
            int max_offset = (total_lines > display_lines) ? (total_lines - display_lines) : 0;
            if (special_key == SCANCODE_ARROW_UP) {
                if (max_offset > 0 && quill_scroll_offset < max_offset)
                    quill_scroll_offset++;
            } else if (special_key == SCANCODE_ARROW_DOWN) {
                if (quill_scroll_offset > 0)
                    quill_scroll_offset--;
            }
            quill_show_scroll_window(buffer, quill_scroll_offset, quill_scroll_window);
            continue;
        }
        if (input_type == 2 && special_key == SPECIAL_CTRL_C) {
            shell_print("\nquill: exit (Ctrl+C).\n");
            break;
        }
        if (my_strcmp(line, ".save") == 0 || my_strcmp(line, ".safe") == 0) {
            if (shell_save_file(name, buffer, strlen(buffer)) == 0)
                shell_print("quill: saved.\n");
            else
                shell_print("quill: save failed.\n");
            continue;
        }
        if (my_strcmp(line, ".quit") == 0)
            break;
        if (my_strcmp(line, ".clear") == 0) {
            buffer[0] = 0;
            quill_scroll_offset = 0;
            continue;
        }
        if (strlen(buffer) + strlen(line) + 2 < NANO_BUFFER_SIZE) {
            strcat(buffer, line);
            strcat(buffer, "\n");
            quill_scroll_offset = 0;
        }
    }
    free(buffer);
}

void shell_sysgraph()
{
    shell_print("=== core flux monitor ===\n");
    uint32_t tick = qk_get_uptime();
    for (int core = 0; core < 4; core++) {
        uint32_t sample = (tick >> core) ^ (tick + core * 97u);
        int bar = (sample % 28) + 3;
        char line[64] = "[core ] ";
        line[5] = '0' + core;
        for (int i = 0; i < bar && i < 50; i++)
            strcat(line, "#");
        strcat(line, "\n");
        shell_print(line);
    }
}

void shell_memscope()
{
    shell_print("=== memscope snapshot ===\n");
    uint32_t seed = qk_get_uptime();
    for (int row = 0; row < 6; row++) {
        uint32_t addr = 0xC0000000u + row * 0x1000u;
        uint32_t val = seed ^ (0xDEADBEEF ^ (row * 0x12345));
        char addr_buf[16], val_buf[16];
        uint_to_hex(addr, addr_buf);
        uint_to_hex(val, val_buf);
        shell_print(addr_buf);
        shell_print("  ");
        shell_print(val_buf);
        shell_print("\n");
    }
}

#include "user/still_alive.h"
#include "user/rick_roll.h"

static void alive_sleep(uint32_t ms)
{
    uint32_t start = qk_get_uptime();
    while (qk_get_uptime() - start < ms)
        qk_yield();
}

static void shell_cmd_alive(void)
{
    uint32_t n = sizeof(still_alive) / sizeof(still_alive[0]);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t dur = still_alive[i].ms;
        if (still_alive[i].freq == 0) {
            qk_pcspk_stop();
            alive_sleep(dur);
        } else {
            uint32_t gap = dur / 7;
            if (gap < 20) gap = 20;
            uint32_t on  = dur - gap;
            qk_pcspk_play(still_alive[i].freq);
            alive_sleep(on);
            qk_pcspk_stop();
            alive_sleep(gap);
        }
    }
    qk_pcspk_stop();
}

static void shell_cmd_rick(void)
{
    uint32_t n = sizeof(rick_roll) / sizeof(rick_roll[0]);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t dur = rick_roll[i].ms;
        if (rick_roll[i].freq == 0) {
            qk_pcspk_stop();
            alive_sleep(dur);
        } else {
            uint32_t gap = dur / 7;
            if (gap < 20) gap = 20;
            uint32_t on  = dur - gap;
            qk_pcspk_play(rick_roll[i].freq);
            alive_sleep(on);
            qk_pcspk_stop();
            alive_sleep(gap);
        }
    }
    qk_pcspk_stop();
}

static void shell_cmd_beep(const char *arg)
{
    uint32_t freq = 880;
    uint32_t duration = 200;
    if (arg && *arg) {
        const char *p = arg;
        while (*p == ' ')
            p++;
        uint32_t parsed = 0;
        int have_num = 0;
        while (*p >= '0' && *p <= '9') {
            have_num = 1;
            parsed = parsed * 10 + (uint32_t)(*p - '0');
            p++;
        }
        if (have_num)
            freq = parsed;
        while (*p == ' ')
            p++;
        parsed = 0;
        have_num = 0;
        while (*p >= '0' && *p <= '9') {
            have_num = 1;
            parsed = parsed * 10 + (uint32_t)(*p - '0');
            p++;
        }
        if (have_num)
            duration = parsed;
    }
    if (freq < 20)
        freq = 20;
    if (freq > 4000)
        freq = 4000;
    if (duration < 10)
        duration = 10;
    if (duration > 2000)
        duration = 2000;
    qk_pcspk_play(freq);
    qk_sleep(duration);
    qk_pcspk_stop();
}

static void shell_spawn_app(const char *path, const char *arg)
{
    uint32_t sz = 0;
    void *buf = shell_read_file(path, &sz);
    if (buf && sz > 0) {
        int pid = qk_spawn(buf, sz);
        if (pid >= 0 && arg && *arg) {
            message_t msg;
            msg.type = MSG_FILE_OPEN;
            strncpy((char *)msg.payload, arg, 127);
            qk_sys_ipc_send(pid, &msg);
        }
        free(buf);
    } else {
        shell_print("Failed to launch: ");
        shell_print(path);
        shell_print("\n");
    }
}

void shell_exec(const char *path)
{
    if (!path || !*path) {
        shell_print("exec: specify a file path.\n");
        return;
    }
    uint32_t size = 0;
    void *buf = shell_read_file(path, &size);
    if (!buf || size == 0) {
        shell_print("exec: unable to read file.\n");
        if (buf)
            free(buf);
        return;
    }
    int pid = qk_spawn(buf, size);
    free(buf);
    if (pid < 0)
        shell_print("exec: spawn failed.\n");
    else {
        char pid_buf[16];
        int_to_str(pid, pid_buf, 0);
        shell_print("exec: started pid ");
        shell_print(pid_buf);
        shell_print("\n");
    }
}

int main()
{
    if (ensure_server_pids() != 0)
        return 1;
    message_t create_msg;
    create_msg.type = MSG_WIN_CREATE;
    create_msg.data1 = 500;
    create_msg.data2 = 350;
    strncpy((char *)create_msg.payload, "Quark Shell", 16);
    qk_sys_ipc_send(comp_pid, &create_msg);

    message_t resp;
    while (1) {
        qk_ipc_recv(&resp);
        if (resp.type == MSG_WIN_CREATED)
            break;
    }

    dir_depth = 0;
    dir_stack[0] = 0;
    current_dir_cluster = 0;
    shell_rebuild_path();
    shell_cd("/USERS/DEFAULT");
    qk_set_proc_name(-1, "shell");

    
    qk_setenv("HOME=/USERS/DEFAULT");
    qk_setenv("PATH=/APPS");
    qk_setenv("SHELL=shell");

    shell_print("QuarkOS Shell Ready\nType 'guide'.\n\n");
    shell_show_prompt();

    char cmd_buffer[64];
    int cmd_ptr = 0;
    memset(cmd_buffer, 0, 64);

    while (1) {
        message_t input_msg;
        qk_ipc_recv(&input_msg);

        if (input_msg.type == MSG_KEY_EVENT) {
            uint32_t raw = input_msg.data1;
            if (raw & 0x80)   
                continue;
            uint8_t scancode = (uint8_t)(raw & 0x7F);
            uint32_t mods    = input_msg.data2;
            char c = scancode_to_char(scancode, mods);
            if (c == '\n') {
                shell_print("\n");
                const char *arg = 0;
                if (cmd_is(cmd_buffer, "guide", "help")) {
                    shell_print("FS: ls, cd, cat, touch, mkdir, hex, clear\n");
                    shell_print(
                        "Apps: notepad [file], imgview [file], sysinfo, quill [file], exec [path]\n");
                    shell_print("Audio: beep [freq] [ms]\n");
                    shell_print("Debug: uptime, procs, kill [-sig] [id], sysgraph, memscope, pf, kp\n");
                    shell_print("Env:   setenv KEY=VALUE, getenv KEY, env\n");
                    shell_print("Apps:  clock, filemgr\n");
                } else if (cmd_is(cmd_buffer, "utest", 0)) {
                    shell_print("--- UTF-8 Render Test ---\n");
                    shell_print("Symbols:  \xe2\x9c\xb0 \xe2\x98\x85 \xe2\x98\x86 \xc2\xa9 \xc2\xae \xc2\xb0 \xe2\x82\xac \xc2\xa3 \xc2\xbd\n");
                    shell_print("Arrows:   \xe2\x86\x90 \xe2\x86\x92 \xe2\x86\x91 \xe2\x86\x93\n");
                    shell_print("Greek:    \xce\xb1 \xce\xb2 \xce\xb3 \xce\xb4 \xce\xb5 \xcf\x80 \xce\xa9\n");
                    shell_print("Box:      \xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90\n");
                    shell_print("          \xe2\x94\x82 box \xe2\x94\x82\n");
                    shell_print("          \xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x98\x98\n");
                    shell_print("Shift:    ! @ # $ % ^ & * ( ) _ + { } | : \" ~\n");
                } else if (cmd_is(cmd_buffer, "purge", "clear"))
                    shell_clear();
                else if (cmd_is(cmd_buffer, "ls", "glimpse"))
                    shell_ls();
                else if (cmd_is(cmd_buffer, "procman", 0))
                    shell_spawn_app("/APPS/SYSINFO.QEX", 0);
                else if (cmd_is(cmd_buffer, "sysinfo", 0))
                    shell_spawn_app("/APPS/SYSINFO.QEX", 0);
                else if (cmd_is(cmd_buffer, "sysgraph", 0))
                    shell_sysgraph();
                else if (cmd_is(cmd_buffer, "memscope", 0))
                    shell_memscope();
                else if (cmd_is(cmd_buffer, "pf", "pagefault"))
                    shell_trigger_page_fault();
                else if (cmd_is(cmd_buffer, "kp", "panic"))
                    shell_trigger_kernel_panic();
                else if (cmd_is(cmd_buffer, "uptime", 0)) {
                    char buf[16];
                    int_to_str(qk_get_uptime(), buf, 0);
                    shell_print("Uptime: ");
                    shell_print(buf);
                    shell_print(" ms\n");
                } else if (cmd_is(cmd_buffer, "procs", 0))
                    shell_procs();
                else if (cmd_is(cmd_buffer, "clock", 0))
                    shell_spawn_app("/APPS/CLOCK.QEX", 0);
                else if (cmd_is(cmd_buffer, "filemgr", 0))
                    shell_spawn_app("/APPS/FILEMGR.QEX", 0);
                else if (cmd_is(cmd_buffer, "notepad", 0))
                    shell_spawn_app("/APPS/NOTEPAD.QEX", 0);
                else if ((arg = cmd_arg_ptr(cmd_buffer, "notepad", 0)) != 0)
                    shell_spawn_app("/APPS/NOTEPAD.QEX", arg);
                else if (cmd_is(cmd_buffer, "imgview", 0))
                    shell_spawn_app("/APPS/IMGVIEW.QEX", 0);
                else if ((arg = cmd_arg_ptr(cmd_buffer, "imgview", 0)) != 0) {
                    static char imgview_path[128];
                    if (arg[0] == '/') {
                        strncpy(imgview_path, arg, sizeof(imgview_path) - 1);
                        imgview_path[sizeof(imgview_path) - 1] = '\0';
                    } else {
                        int plen = 0; while (current_path[plen]) plen++;
                        strncpy(imgview_path, current_path, sizeof(imgview_path) - 1);
                        if (plen > 0 && imgview_path[plen-1] != '/')
                            imgview_path[plen++] = '/';
                        strncpy(imgview_path + plen, arg, sizeof(imgview_path) - 1 - plen);
                        imgview_path[sizeof(imgview_path) - 1] = '\0';
                    }
                    
                    {
                        int ip = 0; while (imgview_path[ip]) ip++;
                        int has_dot = 0;
                        for (int d = ip - 1; d >= 0 && imgview_path[d] != '/'; d--)
                            if (imgview_path[d] == '.') { has_dot = 1; break; }
                        if (!has_dot && ip + 4 < (int)sizeof(imgview_path)) {
                            imgview_path[ip++] = '.';
                            imgview_path[ip++] = 'B';
                            imgview_path[ip++] = 'M';
                            imgview_path[ip++] = 'P';
                            imgview_path[ip]   = '\0';
                        }
                    }
                    shell_spawn_app("/APPS/IMGVIEW.QEX", imgview_path);
                }
                else if ((arg = cmd_arg_ptr(cmd_buffer, "quill", "nano")) != 0)
                    shell_nano(arg);
                else if ((arg = cmd_arg_ptr(cmd_buffer, "drift", "cd")) != 0)
                    shell_cd(arg);
                else if ((arg = cmd_arg_ptr(cmd_buffer, "beam", "cat")) != 0)
                    shell_cat(arg);
                else if ((arg = cmd_arg_ptr(cmd_buffer, "touch", "seed")) != 0)
                    shell_touch(arg);
                else if (cmd_is(cmd_buffer, "touch", "seed"))
                    shell_touch(0);
                else if (cmd_is(cmd_buffer, "alive", 0))
                    shell_cmd_alive();
                else if (cmd_is(cmd_buffer, "rick", 0))
                    shell_cmd_rick();
                else if ((arg = cmd_arg_ptr(cmd_buffer, "beep", 0)) != 0)
                    shell_cmd_beep(arg);
                else if (cmd_is(cmd_buffer, "beep", 0))
                    shell_cmd_beep(0);
                else if ((arg = cmd_arg_ptr(cmd_buffer, "mkdir", "hatch")) != 0)
                    shell_mkdir(arg);
                else if (cmd_is(cmd_buffer, "mkdir", "hatch"))
                    shell_mkdir(0);
                else if ((arg = cmd_arg_ptr(cmd_buffer, "exec", 0)) != 0)
                    shell_exec(arg);
                else if (cmd_is(cmd_buffer, "exec", 0))
                    shell_exec(0);
                else if ((arg = cmd_arg_ptr(cmd_buffer, "quartz", "hex")) != 0)
                    shell_hex(arg);
                else if ((arg = cmd_arg_ptr(cmd_buffer, "setenv", 0)) != 0) {
                    if (qk_setenv(arg) == 0)
                        shell_print("setenv: ok.\n");
                    else
                        shell_print("setenv: failed (use KEY=VALUE format).\n");
                } else if ((arg = cmd_arg_ptr(cmd_buffer, "getenv", 0)) != 0) {
                    char val[128];
                    if (qk_getenv(arg, val, sizeof(val)) == 0) {
                        shell_print(arg);
                        shell_print("=");
                        shell_print(val);
                        shell_print("\n");
                    } else {
                        shell_print("getenv: not set.\n");
                    }
                } else if (cmd_is(cmd_buffer, "env", 0)) {
                    
                    char all[256];
                    if (qk_getenv("HOME", all, sizeof(all)) == 0) { shell_print("HOME="); shell_print(all); shell_print("\n"); }
                    if (qk_getenv("PATH", all, sizeof(all)) == 0) { shell_print("PATH="); shell_print(all); shell_print("\n"); }
                    if (qk_getenv("SHELL", all, sizeof(all)) == 0) { shell_print("SHELL="); shell_print(all); shell_print("\n"); }
                } else if ((arg = cmd_arg_ptr(cmd_buffer, "kill", 0)) != 0) {
                    int pid = 0;
                    int signum = 9; 
                    const char *p = arg;
                    
                    if (*p == '-') {
                        p++;
                        int s = 0;
                        while (*p >= '0' && *p <= '9') { s = s * 10 + (*p - '0'); p++; }
                        while (*p == ' ') p++;
                        signum = s;
                    }
                    while (*p >= '0' && *p <= '9') {
                        pid = pid * 10 + (*p - '0');
                        p++;
                    }
                    if (pid > 0)
                        qk_signal_send(pid, signum);
                } else if (cmd_ptr > 0)
                    shell_print("Unknown command.\n");
                cmd_ptr = 0;
                memset(cmd_buffer, 0, 64);
                shell_show_prompt();
            } else if (c == '\b') {
                if (cmd_ptr > 0) {
                    cmd_ptr--;
                    cmd_buffer[cmd_ptr] = 0;
                    shell_print("\b");
                }
            } else if (c != 0 && cmd_ptr < 63) {
                cmd_buffer[cmd_ptr++] = c;
                char tmp[2] = {c, 0};
                shell_print(tmp);
            }
        }
    }
    return 0;
}
