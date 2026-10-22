#include "abi/ipc.h"
#include "stdbool.h"
#include "stdlib.h"
#include "user/libc/libapi.h"
#include <stddef.h>

#define C_RESET "\033[0m"
#define C_CYAN "\033[36m"
#define C_BCYAN "\033[96m"
#define C_RED "\033[31m"
#define CMD_BUF_SIZE 256
#define HISTORY_MAX 16
#define MAX_PATH_DEPTH 16
#define MAX_PIPELINE_COMMANDS 4
#define SCANCODE_ARROW_UP 0x48
#define SCANCODE_ARROW_DOWN 0x50
#define SCANCODE_CTRL 0x1D

static int master_pid = -1;
static int storage_pid = -1;
static int active_command_pid = -1;
static int active_ctrl_down;
static int launch_input_pipe = -1, launch_output_pipe = -1;
static int defer_launch_message;
static message_t staged_launch_message;
static int pipeline_pids[MAX_PIPELINE_COMMANDS];
static int pipeline_pipe_ids[MAX_PIPELINE_COMMANDS - 1];
static message_t pipeline_launch_messages[MAX_PIPELINE_COMMANDS];
static int pipeline_count;
static uint32_t current_dir_cluster;
static int current_disk;
static char current_path[128] = "/";
static uint32_t dir_stack[MAX_PATH_DEPTH];
static char dir_names[MAX_PATH_DEPTH][16];
static int dir_depth;
static char history[HISTORY_MAX][CMD_BUF_SIZE];
static int history_count;
static int history_pos = -1;

typedef struct {
    int is_dir;
    uint32_t cluster;
    char name[64];
} dir_entry_info_t;

typedef struct {
    char buf[CMD_BUF_SIZE];
    int len;
    int ctrl_down;
} readline_t;

static void tty_print(const char *text) {
    if (text && master_pid >= 0)
        write(1, text, strlen(text));
}

/* Echo typed input directly to the terminal so Enter is displayed before a
 * command can start producing output. Command output still goes through fd 1. */
static void tty_echo(const char *text) {
    if (!text || master_pid < 0) return;
    while (*text) {
        message_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = MSG_WIN_PRINT;
        uint32_t n = 0;
        while (text[n] && n < sizeof(msg.payload)) n++;
        msg.data1 = n;
        memcpy(msg.payload, text, n);
        if (qk_sys_ipc_send(master_pid, &msg) != 0) return;
        text += n;
    }
}

static int equal_folded(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'a' && ca <= 'z') ca -= 'a' - 'A';
        if (cb >= 'a' && cb <= 'z') cb -= 'a' - 'A';
        if (ca != cb) return 0;
    }
    return *a == *b;
}

static char upcase(char c) {
    return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
}

static char scancode_to_char(uint8_t code, uint32_t mods) {
    bool shift = (mods & 1) != 0, caps = (mods & 8) != 0;
    const char *r1 = "1234567890-=", *r1s = "!@#$%^&*()_+";
    const char *r2 = "qwertyuiop[]", *r2s = "QWERTYUIOP{}";
    const char *r3 = "asdfghjkl;'`", *r3s = "ASDFGHJKL:\"~";
    const char *r4 = "zxcvbnm,./", *r4s = "ZXCVBNM<>?";
    bool upper = shift ^ caps;
    if (code >= 0x02 && code <= 0x0D) return shift ? r1s[code - 0x02] : r1[code - 0x02];
    if (code >= 0x10 && code <= 0x1B) return upper ? r2s[code - 0x10] : r2[code - 0x10];
    if (code == 0x2B) return shift ? '|' : '\\';
    if (code >= 0x1E && code <= 0x29) return (code <= 0x26 ? upper : shift) ? r3s[code - 0x1E] : r3[code - 0x1E];
    if (code >= 0x2C && code <= 0x35) return (code <= 0x32 ? upper : shift) ? r4s[code - 0x2C] : r4[code - 0x2C];
    if (code == 0x39) return ' ';
    if (code == 0x1C) return '\n';
    if (code == 0x0E) return '\b';
    return 0;
}

static void rebuild_path(void) {
    int p = 0;
    if (current_disk < 0) {
        current_path[0] = '/'; current_path[1] = 0; return;
    }
    current_path[p++] = '/'; current_path[p++] = 's'; current_path[p++] = 'd';
    current_path[p++] = (char)('0' + current_disk);
    for (int i = 1; i <= dir_depth && p < 120; i++) {
        current_path[p++] = '/';
        for (int j = 0; dir_names[i][j] && p < 126; j++) current_path[p++] = dir_names[i][j];
    }
    current_path[p] = 0;
    char env[140] = "PWD=";
    int n = strlen(env);
    strncpy(env + n, current_path, sizeof(env) - n - 1);
    qk_setenv(env);
}

static const char *display_path(void) {
    static char shown[128];
    char home[64];
    if (qk_getenv("HOME", home, sizeof(home)) == 0) {
        int n = strlen(home);
        if (n < (int)sizeof(shown) && equal_folded(current_path, home) && current_path[n] == 0) {
            shown[0] = '~'; shown[1] = 0; return shown;
        }
        if (n < (int)sizeof(shown) && equal_folded(current_path, home) && current_path[n] == '/') {
            shown[0] = '~';
            strncpy(shown + 1, current_path + n, sizeof(shown) - 2);
            shown[sizeof(shown) - 1] = 0;
            return shown;
        }
    }
    return current_path;
}

static int dir_lookup(uint32_t cluster, const char *target, dir_entry_info_t *out) {
    if (!target || !*target || current_disk < 0) return -1;
    message_t req;
    memset(&req, 0, sizeof(req));
    req.type = MSG_DIR_REQ; req.data1 = cluster; req.data3 = (uint32_t)current_disk;
    qk_sys_ipc_send(storage_pid, &req);
    int found = 0;
    dir_entry_info_t match;
    for (;;) {
        message_t resp;
        qk_ipc_recv(&resp);
        if (resp.sender_pid != storage_pid || resp.type != MSG_DIR_REPLY) continue;
        char *p = (char *)resp.payload;
        while (*p) {
            dir_entry_info_t e;
            e.is_dir = *p++ == '1';
            while (*p && *p != '|') p++;
            if (*p == '|') p++;
            uint32_t c = 0;
            while (*p >= '0' && *p <= '9') c = c * 10 + (uint32_t)(*p++ - '0');
            e.cluster = c;
            if (*p == '|') p++;
            int n = 0;
            while (*p && *p != '\n' && n < (int)sizeof(e.name) - 1) e.name[n++] = *p++;
            e.name[n] = 0;
            if (*p == '\n') p++;
            if (!found && equal_folded(e.name, target)) { match = e; found = 1; }
        }
        if (!resp.data1) break;
    }
    if (found && out) *out = match;
    return found ? 0 : -1;
}

static int search_path_dir(const char *path, uint32_t *cluster_out) {
    if (!path || !cluster_out || current_disk < 0) return -1;
    uint32_t cluster = 0;
    char component[64];
    while (*path == '/') path++;
    while (*path) {
        int n = 0;
        while (*path && *path != '/' && n < (int)sizeof(component) - 1)
            component[n++] = *path++;
        component[n] = 0;
        while (*path == '/') path++;
        if (!n) continue;
        dir_entry_info_t entry;
        if (dir_lookup(cluster, component, &entry) != 0 || !entry.is_dir) return -1;
        cluster = entry.cluster;
    }
    *cluster_out = cluster;
    return 0;
}

static void cd_one(const char *target) {
    if (!target || !*target) return;
    if (target[0] == 's' && target[1] == 'd' && target[2] >= '0' &&
        target[2] <= '9' && target[3] == 0) {
        int d = target[2] - '0';
        if (d >= qk_disk_count()) { tty_print("cd: no such disk\n"); return; }
        current_disk = d; current_dir_cluster = 0; dir_depth = 0;
        memset(dir_stack, 0, sizeof(dir_stack)); memset(dir_names, 0, sizeof(dir_names));
        qk_disk_select(d); rebuild_path(); return;
    }
    int old_disk = current_disk;
    int disk = current_disk;
    int depth = dir_depth;
    uint32_t cluster = current_dir_cluster;
    uint32_t stack[MAX_PATH_DEPTH];
    char names[MAX_PATH_DEPTH][16];
    memcpy(stack, dir_stack, sizeof(stack));
    memcpy(names, dir_names, sizeof(names));
    int absolute = target[0] == '/';
    const char *p = target;
    if (absolute) {
        depth = 0; cluster = 0;
        memset(stack, 0, sizeof(stack)); memset(names, 0, sizeof(names));
        while (*p == '/') p++;
    }
    int failed = 0;
    while (*p) {
        char part[64]; int n = 0;
        while (*p && *p != '/' && n < (int)sizeof(part) - 1) part[n++] = *p++;
        while (*p && *p != '/') p++;
        while (*p == '/') p++;
        part[n] = 0;
        if (!n || equal_folded(part, ".")) continue;
        if (equal_folded(part, "..")) {
            if (depth > 0) cluster = stack[depth--];
            else if (!absolute && disk >= 0) disk = -1;
            continue;
        }
        if ((disk < 0 || (absolute && depth == 0)) && part[0] == 's' &&
            part[1] == 'd' && part[2] >= '0' && part[2] <= '9' && part[3] == 0) {
            int d = part[2] - '0';
            if (d >= qk_disk_count()) { failed = 1; break; }
            disk = d; depth = 0; cluster = 0;
            continue;
        }
        if (disk < 0) { failed = 1; break; }
        current_disk = disk;
        dir_entry_info_t entry;
        if (depth >= MAX_PATH_DEPTH - 1 ||
            dir_lookup(cluster, part, &entry) != 0 || !entry.is_dir) {
            failed = 1; break;
        }
        stack[++depth] = cluster;
        strncpy(names[depth], entry.name, sizeof(names[depth]) - 1);
        names[depth][sizeof(names[depth]) - 1] = 0;
        cluster = entry.cluster;
    }
    current_disk = failed ? old_disk : disk;
    if (failed) { tty_print("cd: not a directory\n"); return; }
    current_dir_cluster = cluster; dir_depth = depth;
    memcpy(dir_stack, stack, sizeof(stack)); memcpy(dir_names, names, sizeof(names));
    if (current_disk >= 0) qk_disk_select(current_disk);
    rebuild_path();
}

static void *read_file(const char *path, uint32_t *size_out) {
    message_t req, resp;
    memset(&req, 0, sizeof(req));
    req.type = MSG_FILE_OPEN;
    req.data3 = (uint32_t)current_disk;
    if (path[0] == '/') {
        req.data1 = 0xFFFF;
        strncpy((char *)req.payload, path, sizeof(req.payload) - 1);
    } else {
        req.data1 = current_dir_cluster;
        strncpy((char *)req.payload, path, 12);
    }
    if (qk_ipc_send_recv(storage_pid, &req, &resp) || resp.type != MSG_FILE_SIZE) return 0;
    uint32_t size = resp.data1;
    if (!size) return 0;
    uint8_t *buf = malloc(size);
    if (!buf) return 0;
    uint32_t off = 0;
    while (off < size) {
        memset(&req, 0, sizeof(req));
        req.type = MSG_REQ_DATA; req.data1 = off; req.data2 = sizeof(resp.payload);
        if (qk_ipc_send_recv(storage_pid, &req, &resp) || resp.type != MSG_FILE_DATA || !resp.data1) break;
        uint32_t got = resp.data1;
        if (got > size - off) got = size - off;
        memcpy(buf + off, resp.payload, got); off += got;
    }
    if (off != size) { free(buf); return 0; }
    *size_out = size;
    return buf;
}

static int command_name_matches(const char *entry, const char *command) {
    int i = 0;
    while (command[i] && entry[i] && upcase(command[i]) == upcase(entry[i])) i++;
    return !command[i] && !entry[i];
}

static int executable_name_matches(const char *entry, const char *command) {
    if (command_name_matches(entry, command)) return 1;
    int n = strlen(command);
    if (n + 4 >= 64) return 0;
    char elf_name[64];
    memcpy(elf_name, command, n);
    memcpy(elf_name + n, ".ELF", 5);
    return command_name_matches(entry, elf_name);
}

static int start_binary_in_dir(const char *name, const char *args,
                               const char *dir_path, uint32_t dir_cluster) {
    message_t req;
    memset(&req, 0, sizeof(req));
    req.type = MSG_DIR_REQ; req.data1 = dir_cluster; req.data3 = (uint32_t)current_disk;
    qk_sys_ipc_send(storage_pid, &req);
    int started = -1;
    for (;;) {
        message_t resp;
        qk_ipc_recv(&resp);
        if (resp.sender_pid != storage_pid || resp.type != MSG_DIR_REPLY) continue;
        char *p = (char *)resp.payload;
        while (*p) {
            int is_dir = *p++ == '1';
            while (*p && *p != '|') p++;
            if (*p == '|') p++;
            while (*p >= '0' && *p <= '9') p++;
            if (*p == '|') p++;
            char entry[64]; int n = 0;
            while (*p && *p != '\n' && n < (int)sizeof(entry) - 1) entry[n++] = *p++;
            entry[n] = 0;
            if (*p == '\n') p++;
            if (is_dir || !executable_name_matches(entry, name)) continue;
            char path[128];
            int plen = 0, base_len = strlen(dir_path), elen = strlen(entry);
            int prefix_len = dir_path[0] == '/' ? (int)base_len :
                             (int)strlen(current_path) + 1 + (int)base_len;
            if (prefix_len + 1 + elen >= (int)sizeof(path)) continue;
            if (dir_path[0] == '/') {
                memcpy(path, dir_path, base_len); plen = base_len;
            } else {
                plen = strlen(current_path);
                memcpy(path, current_path, plen);
                if (plen < (int)sizeof(path) - 1 && path[plen - 1] != '/') path[plen++] = '/';
                memcpy(path + plen, dir_path, base_len); plen += base_len;
            }
            if (plen && path[plen - 1] != '/') path[plen++] = '/';
            if (elen > (int)sizeof(path) - plen - 1) elen = sizeof(path) - plen - 1;
            memcpy(path + plen, entry, elen); path[plen + elen] = 0;
            uint32_t size = 0;
            uint8_t *binary = read_file(path, &size);
            if (!binary || size > 0x4000000U - 8 * 1024 * 1024 - 20) {
                free(binary);
                return -1;
            }

            int pid = qk_spawn(binary, size);
            free(binary);
            if (pid < 0) return -1;
            if ((launch_input_pipe >= 0 &&
                 qk_fd_set(pid, STDIN_FILENO, launch_input_pipe, QK_FD_PIPE_READ) != 0) ||
                (launch_output_pipe >= 0 &&
                 qk_fd_set(pid, STDOUT_FILENO, launch_output_pipe, QK_FD_PIPE_WRITE) != 0)) {
                qk_sys_kill(pid);
                return -1;
            }
            message_t launch;
            memset(&launch, 0, sizeof(launch));
            launch.type = MSG_CLI_START;
            launch.data1 = current_dir_cluster;
            launch.data2 = (uint32_t)current_disk;
            launch.data3 = 0;
            strncpy((char *)launch.payload, args, sizeof(launch.payload) - 1);
            if (defer_launch_message) {
                staged_launch_message = launch;
            } else if (qk_sys_ipc_send(pid, &launch) != 0) {
                qk_sys_kill(pid);
                return -1;
            }
            started = pid;
            break;
        }
        if (started >= 0 || !resp.data1) break;
    }
    if (started >= 0) active_command_pid = started;
    return started;
}

static int start_binary(const char *name, const char *args) {
    if (current_disk < 0) return -1;
    char path_list[128];
    if (qk_getenv("PATH", path_list, sizeof(path_list)) != 0 || !path_list[0])
        strncpy(path_list, "/BIN", sizeof(path_list));
    const char *p = path_list;
    while (*p) {
        char dir[64];
        int n = 0;
        while (*p && *p != ':' && n < (int)sizeof(dir) - 1) dir[n++] = *p++;
        while (*p && *p != ':') p++;
        dir[n] = 0;
        if (*p == ':') p++;
        if (!n) {
            strncpy(dir, current_path, sizeof(dir) - 1);
            dir[sizeof(dir) - 1] = 0;
            if (start_binary_in_dir(name, args, dir, current_dir_cluster) >= 0)
                return active_command_pid;
            continue;
        }
        uint32_t cluster;
        if (search_path_dir(dir, &cluster) == 0) {
            int pid = start_binary_in_dir(name, args, dir, cluster);
            if (pid >= 0) return pid;
        }
    }
    return -1;
}

static int launch_line_command(const char *line, int input_pipe, int output_pipe,
                               message_t *launch_message) {
    while (*line == ' ' || *line == '\t') line++;
    if (!*line) return -1;
    char name[64]; int n = 0;
    while (line[n] && line[n] != ' ' && line[n] != '\t' && n < (int)sizeof(name) - 1) n++;
    memcpy(name, line, n); name[n] = 0;
    const char *args = line + n;
    while (*args == ' ' || *args == '\t') args++;
    launch_input_pipe = input_pipe;
    launch_output_pipe = output_pipe;
    defer_launch_message = launch_message != 0;
    int pid = start_binary(name, args);
    defer_launch_message = 0;
    if (pid >= 0 && launch_message)
        *launch_message = staged_launch_message;
    launch_input_pipe = launch_output_pipe = -1;
    return pid;
}

static void close_pipeline_pipe(int i) {
    if (i >= 0 && i < MAX_PIPELINE_COMMANDS - 1 && pipeline_pipe_ids[i] >= 0) {
        qk_pipe_close(pipeline_pipe_ids[i]);
        pipeline_pipe_ids[i] = -1;
    }
}

static void run_pipeline(char *commands[], int count) {
    pipeline_count = count;
    for (int i = 0; i < MAX_PIPELINE_COMMANDS; i++) pipeline_pids[i] = -1;
    for (int i = 0; i < MAX_PIPELINE_COMMANDS - 1; i++) pipeline_pipe_ids[i] = -1;
    for (int i = 0; i < count - 1; i++) {
        pipeline_pipe_ids[i] = qk_pipe_create("shellpipe");
        if (pipeline_pipe_ids[i] < 0) {
            for (int j = 0; j < i; j++) close_pipeline_pipe(j);
            pipeline_count = 0;
            tty_print("shell: cannot create pipe\n");
            return;
        }
    }
    /* Attach every stage before running any command, so readers see their writers
     * and producers see their readers from the start. */
    for (int i = count - 1; i >= 0; i--) {
        int input = i ? pipeline_pipe_ids[i - 1] : -1;
        int output = i + 1 < count ? pipeline_pipe_ids[i] : -1;
        pipeline_pids[i] = launch_line_command(commands[i], input, output,
                                                &pipeline_launch_messages[i]);
        if (pipeline_pids[i] < 0) {
            for (int j = 0; j < count; j++) if (pipeline_pids[j] >= 0) qk_sys_kill(pipeline_pids[j]);
            for (int j = 0; j < count - 1; j++) close_pipeline_pipe(j);
            pipeline_count = 0;
            tty_print(C_RED "command not found: " C_RESET); tty_print(commands[i]); tty_print("\n");
            return;
        }
    }
    for (int i = count - 1; i >= 0; i--) {
        if (qk_sys_ipc_send(pipeline_pids[i], &pipeline_launch_messages[i]) != 0) {
            for (int j = 0; j < count; j++) if (pipeline_pids[j] >= 0) qk_sys_kill(pipeline_pids[j]);
            for (int j = 0; j < count - 1; j++) close_pipeline_pipe(j);
            pipeline_count = 0;
            tty_print("shell: cannot start pipeline\n");
            return;
        }
    }
    active_command_pid = pipeline_pids[count - 1];
}

static int run_command(const char *line) {
    char parsed[CMD_BUF_SIZE];
    int out = 0;
    char quote = 0;
    int pipe_positions[MAX_PIPELINE_COMMANDS - 1], pipe_count = 0;
    for (int i = 0; line[i] && out < CMD_BUF_SIZE - 1; i++) {
        char c = line[i];
        if (!quote && (c == '\'' || c == '"')) { quote = c; continue; }
        if (quote && c == quote) { quote = 0; continue; }
        int escaped = 0;
        if (c == '\\' && quote != '\'' && line[i + 1]) { c = line[++i]; escaped = 1; }
        if (!quote && !escaped && c == '|') {
            if (pipe_count < MAX_PIPELINE_COMMANDS - 1) pipe_positions[pipe_count++] = out;
            else { tty_print("syntax error: pipeline has too many commands\n"); return 0; }
        }
        parsed[out++] = c;
    }
    if (quote) { tty_print("syntax error: unmatched quote\n"); return 0; }
    parsed[out] = 0;
    if (pipe_count) {
        char *commands[MAX_PIPELINE_COMMANDS];
        int start = 0;
        for (int i = 0; i <= pipe_count; i++) {
            int end = i < pipe_count ? pipe_positions[i] : out;
            if (i < pipe_count) parsed[end] = 0;
            char *part = parsed + start;
            while (*part == ' ' || *part == '\t') part++;
            int len = strlen(part);
            while (len && (part[len - 1] == ' ' || part[len - 1] == '\t')) part[--len] = 0;
            if (!*part) { tty_print("syntax error: incomplete pipeline\n"); return 0; }
            commands[i] = part;
            start = end + 1;
        }
        tty_print(C_CYAN "--- pipeline output --------------------------------" C_RESET "\n");
        run_pipeline(commands, pipe_count + 1);
        return 1;
    }
    line = parsed;
    while (*line == ' ' || *line == '\t') line++;
    if (!*line) return 0;
    char name[64]; int n = 0;
    while (line[n] && line[n] != ' ' && line[n] != '\t' && n < (int)sizeof(name) - 1) n++;
    memcpy(name, line, n); name[n] = 0;
    const char *args = line + n;
    while (*args == ' ' || *args == '\t') args++;
    if (equal_folded(name, "cd")) {
        if (!*args) {
            current_disk = 0; current_dir_cluster = 0; dir_depth = 0; dir_stack[0] = 0;
            cd_one("USR"); cd_one("DEFAULT");
        } else cd_one(args);
        return 0;
    }
    if (equal_folded(name, "setenv")) {
        int has_equals = 0;
        for (const char *p = args; *p; p++) if (*p == '=') has_equals = 1;
        if (!*args || !has_equals || qk_setenv(args) != 0)
            tty_print("setenv: use KEY=VALUE\n");
        return 0;
    }
    if (equal_folded(name, "exit")) qk_sys_exit(0);
    if (start_binary(name, args) < 0) {
        tty_print(C_RED "command not found: " C_RESET); tty_print(name); tty_print("\n");
        return 0;
    }
    return 1;
}

static void show_prompt(void) {
    tty_print(C_CYAN); tty_print(display_path()); tty_print(C_RESET " $ ");
}

static void readline_reset(readline_t *rl) {
    memset(rl->buf, 0, sizeof(rl->buf)); rl->len = 0; rl->ctrl_down = 0; history_pos = -1;
}

static void history_push(const char *line) {
    if (!*line) return;
    int idx = history_count % HISTORY_MAX;
    strncpy(history[idx], line, CMD_BUF_SIZE - 1); history[idx][CMD_BUF_SIZE - 1] = 0;
    history_count++;
}

static void readline_replace(readline_t *rl, const char *s) {
    for (int i = 0; i < rl->len; i++) tty_echo("\b \b");
    strncpy(rl->buf, s, CMD_BUF_SIZE - 1); rl->buf[CMD_BUF_SIZE - 1] = 0;
    rl->len = strlen(rl->buf); tty_echo(rl->buf);
}

static int readline_feed(readline_t *rl, uint32_t raw, uint32_t mods, int *ctrl_c) {
    *ctrl_c = 0;
    int released = (raw & 0x80) != 0, ext = (raw & 0x100) != 0;
    uint8_t sc = (uint8_t)(raw & 0x7F);
    if (released) { if (sc == SCANCODE_CTRL) rl->ctrl_down = 0; return 0; }
    if (sc == SCANCODE_CTRL) { rl->ctrl_down = 1; return 0; }
    if (ext && sc == SCANCODE_ARROW_UP) {
        if (history_count) { if (history_pos < 0) history_pos = history_count - 1; if (history_pos >= 0) readline_replace(rl, history[history_pos % HISTORY_MAX]); }
        return 0;
    }
    if (ext && sc == SCANCODE_ARROW_DOWN) {
        if (history_pos >= 0) { history_pos++; if (history_pos >= history_count) { history_pos = -1; readline_replace(rl, ""); } else readline_replace(rl, history[history_pos % HISTORY_MAX]); }
        return 0;
    }
    if (rl->ctrl_down && sc == 0x2E) { *ctrl_c = 1; return 1; }
    char c = scancode_to_char(sc, mods);
    if (!c) return 0;
    if (c == '\n') { tty_echo("\r\n"); return 1; }
    if (c == '\b') { if (rl->len) { rl->buf[--rl->len] = 0; tty_echo("\b \b"); } return 0; }
    if (rl->len < CMD_BUF_SIZE - 1) { rl->buf[rl->len++] = c; rl->buf[rl->len] = 0; char t[2] = {c, 0}; tty_echo(t); }
    return 0;
}

static void start_home(void) {
    current_disk = 0; current_dir_cluster = 0; dir_depth = 0; dir_stack[0] = 0;
    rebuild_path();
    cd_one("USR"); cd_one("DEFAULT");
    qk_setenv("HOME=/sd0/USR/DEFAULT");
}

int main(void) {
    storage_pid = qk_get_server_pid(QK_SERVER_STORAGE);
    qk_set_proc_name(-1, "reactty");
    qk_register_as_server(QK_SERVER_TTYSH);
    while (master_pid < 0) {
        message_t msg; qk_ipc_recv(&msg);
        if (msg.type == MSG_TERM_ATTACH) master_pid = (int)msg.data1;
    }
    start_home();
    qk_setenv("PATH=/BIN"); qk_setenv("SHELL=reactty");
    tty_print(C_BCYAN "Sodium reactty" C_RESET "  --  type 'help' for commands\n");
    show_prompt();
    readline_t rl; readline_reset(&rl);
    for (;;) {
        message_t msg; qk_ipc_recv(&msg);
        if (active_command_pid >= 0 && msg.sender_pid == active_command_pid &&
            (msg.type == MSG_WIN_PRINT || msg.type == MSG_WIN_CLEAR)) {
            qk_sys_ipc_send(master_pid, &msg);
            continue;
        }
        if (msg.type == MSG_TTY_OUTPUT) {
            msg.type = MSG_WIN_PRINT;
            qk_sys_ipc_send(master_pid, &msg);
            continue;
        }
        if (msg.sender_pid == 0 && msg.type == MSG_PROCESS_TERMINATED) {
            int dead_pid = (int)msg.data1;
            if (pipeline_count > 0) {
                for (int i = 0; i < pipeline_count; i++) {
                    if (dead_pid == pipeline_pids[i]) {
                        pipeline_pids[i] = -1;
                        close_pipeline_pipe(i - 1);
                        close_pipeline_pipe(i);
                        break;
                    }
                }
                int running = 0;
                for (int i = 0; i < pipeline_count; i++) if (pipeline_pids[i] >= 0) running++;
                if (!running) {
                    active_command_pid = -1;
                    pipeline_count = 0;
                    show_prompt();
                }
            } else if (dead_pid == active_command_pid) {
                active_command_pid = -1; show_prompt();
            }
            continue;
        }
        if (msg.type != MSG_KEY_EVENT) continue;
        if (pipeline_count > 0) {
            uint32_t raw = msg.data1;
            int released = (raw & 0x80) != 0;
            int ext = (raw & 0x100) != 0;
            uint8_t sc = (uint8_t)(raw & 0x7f);
            if (sc == SCANCODE_CTRL) { active_ctrl_down = !released; continue; }
            if (!released && !ext && active_ctrl_down && sc == 0x2e) {
                qk_tty_input(3);
                for (int i = 0; i < pipeline_count; i++) if (pipeline_pids[i] >= 0) qk_sys_kill(pipeline_pids[i]);
                for (int i = 0; i < pipeline_count - 1; i++) close_pipeline_pipe(i);
                tty_print("^C\n");
            }
            continue;
        }
        if (active_command_pid >= 0) {
            uint32_t raw = msg.data1;
            int released = (raw & 0x80) != 0;
            int ext = (raw & 0x100) != 0;
            uint8_t sc = (uint8_t)(raw & 0x7f);
            if (sc == SCANCODE_CTRL) { active_ctrl_down = !released; continue; }
            if (released || ext) continue;
            if (active_ctrl_down && sc == 0x2e) {
                qk_tty_input(3);
                qk_sys_kill(active_command_pid);
                active_command_pid = -1;
                tty_print("^C\n"); show_prompt();
                continue;
            }
            if (active_ctrl_down && sc == 0x20) {
                qk_tty_input(4);
                continue;
            }
            char c = scancode_to_char(sc, msg.data2);
            if (!c) continue;
            if (qk_tty_input((uint8_t)c) == 0) {
                if (c == '\b') tty_echo("\b \b");
                else if (c == '\n') tty_echo("\r\n");
                else { char t[2] = {c, 0}; tty_echo(t); }
            }
            continue;
        }
        int ctrl_c = 0;
        if (!readline_feed(&rl, msg.data1, msg.data2, &ctrl_c)) continue;
        if (ctrl_c) tty_print("^C\n");
        else { history_push(rl.buf); run_command(rl.buf); }
        readline_reset(&rl);
        if (active_command_pid < 0) show_prompt();
    }
}
