#ifndef SYSCALLS_H
#define SYSCALLS_H
#include "abi/ipc.h"
#include "stdint.h"
#include "kernel/lib/util.h"
#define USER_FB_VIRT_START 0x80000000
typedef struct {
    int id;
    int x;
    int y;
    int w;
    int h;
    uint32_t color;
} draw_rect_args_t;
typedef struct {
    int id;
    int x;
    int y;
    const char *str;
    uint32_t color;
} draw_text_args_t;
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    void *buffer;
} fb_info_t;
typedef struct {
    uint32_t start;
    uint32_t end;
    uint32_t flags;
    char name[16];
} vmap_region_t;
uint32_t sys_get_uptime();
int sys_sleep(uint32_t ms);
int sys_exec(const char *filename);
int sys_read_file(const char *filename, char *user_buf);
void k_syscall_entry(struct InterruptRegisters *regs);
int sys_map_framebuffer(fb_info_t *info);
int sys_irq_wait(int irq);
int sys_process_create(void *buffer, uint32_t size);
int sys_set_server_pid(int type, int pid);
int sys_get_server_pid(int type);
int sys_ipc_send(int dest_pid, message_t *msg_user);
int sys_ipc_recv(message_t *msg_user);
char *strncpy(char *dest, const char *src, uint32_t n);
int sys_get_vmap(vmap_region_t *out_regions);
int sys_power_action(int action);
int sys_get_stats(uint32_t *used, uint32_t *total, uint32_t *cpu);
int sys_trigger_panic(const char *msg);
#endif
