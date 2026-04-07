#include "libapi.h"
#include "lib/stdlib.h"

static inline int syscall(int a, int b, int c, int d)
{
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(a), "b"(b), "c"(c), "d"(d) : "memory");
    return ret;
}

void qk_sys_exit(int code)
{
    syscall(0, code, 0, 0);
    while (1)
        ;
}
uint32_t qk_get_uptime()
{
    return (uint32_t)syscall(6, 0, 0, 0);
}
void qk_sleep(uint32_t ms)
{
    syscall(7, ms, 0, 0);
}
void qk_yield(void)
{
    syscall(48, 0, 0, 0);
}
void qk_wait_irq(int irq)
{
    syscall(16, irq, 0, 0);
}
int qk_spawn(void *buffer, uint32_t size)
{
    return syscall(9, (int)buffer, (int)size, 0);
}
int qk_get_server_pid(qk_server_type_t type)
{
    return syscall(46, (int)type, 0, 0);
}
int qk_map_framebuffer(fb_info_t *f)
{
    return syscall(11, (int)f, 0, 0);
}
int qk_ipc_recv_nb(message_t *m)
{
    return syscall(12, (int)m, 0, 0);
}
int qk_sys_ipc_send(int pid, message_t *m)
{
    return syscall(13, pid, (int)m, 0);
}
int qk_ipc_recv(message_t *m)
{
    return syscall(14, (int)m, 0, 0);
}
int qk_ipc_send_recv(int pid, message_t *msg, message_t *reply)
{
    return syscall(15, pid, (int)msg, (int)reply);
}
int qk_sbrk(uint32_t size)
{
    return syscall(26, (int)size, 0, 0);
}
int qk_map_wallpaper(void **ptr)
{
    return syscall(33, (int)ptr, 0, 0);
}
int qk_map_font_atlas(void **ptr)
{
    return syscall(25, (int)ptr, 0, 0);
}
int qk_map_icons_atlas(void **ptr)
{
    return syscall(32, (int)ptr, 0, 0);
}
int qk_get_vmap(vmap_region_t *out)
{
    return syscall(35, (int)out, 0, 0);
}
int qk_get_time(qk_time_t *t)
{
    return syscall(36, (int)t, 0, 0);
}
int qk_get_stats(uint32_t *u, uint32_t *t, uint32_t *cpu)
{
    return syscall(37, (int)u, (int)t, (int)cpu);
}
int qk_get_procs(process_info_t *out, int max)
{
    return syscall(40, (int)out, max, 0);
}
int qk_set_proc_name(int pid, const char *name)
{
    return syscall(41, pid, (int)name, 0);
}
int qk_sys_kill(int pid)
{
    return syscall(24, pid, 0, 0);
}
void sys_window_close(int id)
{
    syscall(24, id, 0, 0);
}

uint8_t qk_hw_inb(uint16_t p)
{
    return (uint8_t)syscall(20, p, 0, 0);
}
void qk_hw_outb(uint16_t p, uint8_t v)
{
    syscall(21, p, v, 0);
}
uint16_t qk_inw(uint16_t p)
{
    return (uint16_t)syscall(22, p, 0, 0);
}
void qk_outw(uint16_t p, uint16_t v)
{
    syscall(23, p, v, 0);
}

void qk_print_at(int x, int y, uint32_t color, const char *str)
{
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

void qk_power_shutdown(void)
{
    syscall(45, 0, 0, 0);
}
void qk_power_reboot(void)
{
    syscall(45, 1, 0, 0);
}
void qk_trigger_panic(const char *msg)
{
    syscall(47, (int)msg, 0, 0);
}

void qk_pcspk_play(uint32_t freq)
{
    syscall(50, (int)freq, 0, 0);
}

void qk_pcspk_stop(void)
{
    syscall(51, 0, 0, 0);
}

void qk_pcspk_pwm_play(const uint8_t *buf, uint32_t len, uint32_t rate)
{
    syscall(52, (int)buf, (int)len, (int)rate);
}

int qk_pcspk_pwm_ready(void)
{
    return syscall(53, 0, 0, 0);
}
