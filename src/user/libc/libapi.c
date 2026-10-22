#include "libapi.h"
#include "stdlib.h"
static inline long syscall(long a, long b, long c, long d) {
    long ret;
    register long x0 asm("x0") = a;
    register long x1 asm("x1") = b;
    register long x2 asm("x2") = c;
    register long x3 asm("x3") = d;
    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "memory");
    ret = x0;
    return ret;
}
void qk_sys_exit(int code) {
    syscall(0, code, 0, 0);
    while (1)
        ;
}
uint32_t qk_get_uptime() {
    return (uint32_t)syscall(6, 0, 0, 0);
}
void qk_sleep(uint32_t ms) {
    syscall(7, ms, 0, 0);
}
void qk_yield(void) {
    syscall(48, 0, 0, 0);
}
int qk_spawn(void *buffer, uint32_t size) {
    return syscall(9, (long)buffer, (long)size, 0);
}
int qk_get_server_pid(qk_server_type_t type) {
    return syscall(46, (int)type, 0, 0);
}
int qk_register_as_server(qk_server_type_t type) {
    return syscall(19, (int)type, qk_getpid(), 0);
}
int qk_map_framebuffer(fb_info_t *f) {
    return syscall(11, (long)f, 0, 0);
}
void qk_gpu_flush(void) {
    syscall(74, 0, 0, 0);
}
void qk_gpu_flush_rect(int x, int y, int w, int h) {
    uint32_t xy = ((uint32_t)x << 16) | ((uint32_t)y & 0xFFFF);
    uint32_t wh = ((uint32_t)w << 16) | ((uint32_t)h & 0xFFFF);
    syscall(74, (long)xy, (long)wh, 0);
}
int qk_ipc_recv_nb(message_t *m) {
    return syscall(12, (long)m, 0, 0);
}
int qk_sys_ipc_send(int pid, message_t *m) {
    return syscall(13, pid, (long)m, 0);
}
int qk_ipc_recv(message_t *m) {
    return syscall(14, (long)m, 0, 0);
}
int qk_ipc_send_recv(int pid, message_t *msg, message_t *reply) {
    return syscall(15, pid, (long)msg, (long)reply);
}
int qk_sbrk(uint32_t size) {
    return syscall(26, (long)size, 0, 0);
}
int qk_map_wallpaper(void **ptr) {
    return syscall(33, (long)ptr, 0, 0);
}
int qk_map_font_atlas(void **ptr) {
    return syscall(25, (long)ptr, 0, 0);
}
int qk_get_vmap(vmap_region_t *out) {
    return syscall(35, (long)out, 0, 0);
}
int qk_get_time(qk_time_t *t) {
    return syscall(36, (long)t, 0, 0);
}
int qk_get_stats(uint32_t *u, uint32_t *t, uint32_t *cpu) {
    return syscall(37, (long)u, (long)t, (long)cpu);
}
int qk_get_procs(process_info_t *out, int max) {
    return syscall(40, (long)out, max, 0);
}
int qk_set_proc_name(int pid, const char *name) {
    return syscall(41, pid, (long)name, 0);
}
int qk_sys_kill(int pid) {
    return syscall(24, pid, 0, 0);
}
void sys_window_close(int id) {
    syscall(24, id, 0, 0);
}
uint8_t qk_input_compat_read(uint16_t p) {
    return (uint8_t)syscall(20, p, 0, 0);
}
int qk_blk_read(uint32_t sector, uint32_t count, void *buf) {
    return (int)syscall(80, (long)sector, (long)count, (long)buf);
}
int qk_blk_write(uint32_t sector, uint32_t count, const void *buf) {
    return (int)syscall(81, (long)sector, (long)count, (long)buf);
}
int qk_disk_count(void) {
    return (int)syscall(82, 0, 0, 0);
}
int qk_disk_info(int disk_idx, disk_info_t *out) {
    return (int)syscall(83, (long)disk_idx, (long)out, 0);
}
int qk_disk_read(int disk_idx, uint32_t sector, uint32_t count, void *buf) {
    blk_io_req_t req = {.sector = sector, .count = count, .buf = buf};
    return (int)syscall(84, (long)disk_idx, (long)&req, 0);
}
int qk_disk_write(int disk_idx, uint32_t sector, uint32_t count, const void *buf) {
    blk_io_req_t req = {.sector = sector, .count = count, .buf = (void *)buf};
    return (int)syscall(85, (long)disk_idx, (long)&req, 0);
}
void qk_disk_select(int disk_idx) {
    syscall(86, (long)disk_idx, 0, 0);
}
int qk_virt_to_phys_list(uint32_t virt, uint32_t size, uint32_t *out) {
    return syscall(56, (long)virt, (long)size, (long)out);
}
uint32_t qk_map_phys_list(uint32_t *phys_pages, uint32_t count) {
    return (uint32_t)syscall(57, (long)phys_pages, (long)count, 0);
}
void *qk_map_foreign_mem(int src_pid, uint32_t src_virt, uint32_t size) {
    uint32_t mapped = (uint32_t)syscall(58, src_pid, (long)src_virt, (long)size);
    return mapped ? (void *)(uintptr_t)mapped : 0;
}
int qk_grant_foreign_map(int dest_pid, uint32_t src_virt, uint32_t size) {
    return (int)syscall(59, dest_pid, (long)src_virt, (long)size);
}
void *qk_win_map_buffer(int comp_pid, int win_id, int *w_out, int *h_out) {
    message_t msg, reply;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_WIN_MAP_BUFFER;
    msg.data1 = (uint32_t)win_id;
    if (qk_ipc_send_recv(comp_pid, &msg, &reply) != 0)
        return 0;
    if (reply.type != MSG_WIN_BUFFER_INFO || reply.data1 == 0)
        return 0;
    if (w_out)
        *w_out = (long)reply.data2;
    if (h_out)
        *h_out = (long)reply.data3;
    uint32_t src_virt = ((uint32_t *)reply.payload)[0];
    uint32_t size = reply.data1;
    uint32_t mapped = (uint32_t)syscall(58, comp_pid, (long)src_virt, (long)size);
    if (!mapped)
        return 0;
    return (void *)(uintptr_t)mapped;
}
void qk_win_dirty(int comp_pid) {
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_WIN_DIRTY;
    qk_sys_ipc_send(comp_pid, &msg);
}
void qk_print_at(int x, int y, uint32_t color, const char *str) {
    message_t msg;
    msg.type = 59;
    msg.data1 = x;
    msg.data2 = y;
    msg.data3 = color;
    strncpy((char *)msg.payload, str, 127);
    static int comp_pid_cache = -1;
    if (comp_pid_cache < 0)
        comp_pid_cache = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    if (comp_pid_cache < 0)
        return;
    qk_sys_ipc_send(comp_pid_cache, &msg);
}
void qk_blit_pixels(int comp_pid, int x, int y, uint32_t *pixels, int count) {
    while (count > 0) {
        int batch = count > 128 ? 128 : count;
        message_t msg;
        msg.type = MSG_WIN_BLIT_PIXELS;
        msg.data1 = (uint32_t)x;
        msg.data2 = (uint32_t)y;
        msg.data3 = (uint32_t)batch;
        memcpy(msg.payload, pixels, (uint32_t)(batch * 4));
        qk_sys_ipc_send(comp_pid, &msg);
        x += batch;
        pixels += batch;
        count -= batch;
    }
}
void qk_compositor_suspend(int comp_pid) {
    message_t msg;
    msg.type = MSG_COMPOSITOR_SUSPEND;
    msg.data1 = msg.data2 = msg.data3 = 0;
    qk_sys_ipc_send(comp_pid, &msg);
}
void qk_compositor_resume(int comp_pid) {
    message_t msg;
    msg.type = MSG_COMPOSITOR_RESUME;
    msg.data1 = msg.data2 = msg.data3 = 0;
    qk_sys_ipc_send(comp_pid, &msg);
}
int qk_getpid(void) {
    return syscall(55, 0, 0, 0);
}
void qk_input_redirect(int target_pid) {
    int input_pid = qk_get_server_pid(QK_SERVER_INPUT);
    if (input_pid <= 0)
        return;
    message_t msg;
    msg.type = MSG_INPUT_REDIRECT;
    msg.data1 = (uint32_t)target_pid;
    msg.data2 = msg.data3 = 0;
    qk_sys_ipc_send(input_pid, &msg);
}
void qk_input_restore(void) {
    int input_pid = qk_get_server_pid(QK_SERVER_INPUT);
    if (input_pid <= 0)
        return;
    message_t msg;
    msg.type = MSG_INPUT_RESTORE;
    msg.data1 = msg.data2 = msg.data3 = 0;
    qk_sys_ipc_send(input_pid, &msg);
}
int qk_input_inject(const Event *event, uint32_t modifiers) {
    if (!event || (event->type != EVENT_KEY_PRESS && event->type != EVENT_KEY_RELEASE))
        return -1;
    int input_pid = qk_get_server_pid(QK_SERVER_INPUT);
    if (input_pid <= 0)
        return -1;
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_INPUT_INJECT;
    msg.data1 = event->key | (event->type == EVENT_KEY_RELEASE ? 0x80U : 0);
    msg.data2 = modifiers;
    memcpy(msg.payload, event, sizeof(*event));
    return qk_sys_ipc_send(input_pid, &msg);
}
void qk_power_shutdown(void) {
    syscall(45, 0, 0, 0);
}
void qk_power_reboot(void) {
    syscall(45, 1, 0, 0);
}
void qk_trigger_panic(const char *msg) {
    syscall(47, (long)msg, 0, 0);
}
int qk_waitpid(int pid) {
    return syscall(64, pid, 0, 0);
}
int qk_pipe_create(const char *name) {
    return syscall(65, (long)name, 0, 0);
}
int qk_pipe_open(const char *name) {
    return syscall(66, (long)name, 0, 0);
}
int qk_pipe_write(int pipe_id, const uint8_t *buf, uint32_t len) {
    return syscall(67, pipe_id, (long)buf, (long)len);
}
int qk_pipe_read(int pipe_id, uint8_t *buf, uint32_t len) {
    return syscall(68, pipe_id, (long)buf, (long)len);
}
int qk_pipe_close(int pipe_id) {
    return syscall(69, pipe_id, 0, 0);
}
int qk_read(int fd, void *buf, uint32_t count) {
    return (int)syscall(87, fd, (long)buf, count);
}
int qk_write(int fd, const void *buf, uint32_t count) {
    return (int)syscall(88, fd, (long)buf, count);
}
int qk_tty_input(uint8_t ch) {
    return (int)syscall(89, ch, 0, 0);
}
int qk_fd_set(int pid, int fd, int object, int kind) {
    return (int)syscall(91, pid, (fd & 0xff) | ((kind & 0xff) << 8), object);
}
int read(int fd, void *buf, unsigned int count) {
    return qk_read(fd, buf, count);
}
int write(int fd, const void *buf, unsigned int count) {
    return qk_write(fd, buf, count);
}
int qk_set_alarm(uint32_t ms) {
    return syscall(70, (long)ms, 0, 0);
}
int qk_setenv(const char *kv) {
    return syscall(71, (long)kv, 0, 0);
}
int qk_getenv(const char *key, char *out, uint32_t maxlen) {
    return syscall(72, (long)key, (long)out, (long)maxlen);
}
int qk_signal_send(int pid, int signum) {
    return syscall(73, pid, signum, 0);
}
