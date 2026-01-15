#ifndef SYSCALLS_H
#define SYSCALLS_H

#include "lib/stdint.h"
#include "video/window.h"
#include "util/util.h"

typedef struct
{
    int id;
    int x;
    int y;
    int w;
    int h;
    uint32_t color;
} draw_rect_args_t;

typedef struct
{
    int id;
    int x;
    int y;
    const char *str;
    uint32_t color;
} draw_text_args_t;

Window *get_window_by_id(int id);
int sys_window_create(int w, int h, char *title);
void sys_draw_rect(draw_rect_args_t *args);
void sys_draw_text(draw_text_args_t *args);
uint32_t sys_get_uptime();
void sys_sleep(uint32_t ms);
void syscall_handler(struct InterruptRegisters *regs);

#endif