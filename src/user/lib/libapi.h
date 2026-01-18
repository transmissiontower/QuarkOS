#ifndef LIBAPI_H
#define LIBAPI_H

#include "lib/stdint.h"

typedef struct {
    int id; int x; int y; int w; int h; uint32_t color;
} draw_rect_args_t;

typedef struct {
    int id; int x; int y; const char* str; uint32_t color;
} draw_text_args_t;

void qk_exit(int code);
int qk_window_create(int w, int h, const char* title);
void qk_draw_rect(int win_id, int x, int y, int w, int h, uint32_t color);
void qk_draw_text(int win_id, int x, int y, const char* str, uint32_t color);
uint32_t qk_get_uptime();
void qk_sleep(uint32_t ms);
int qk_get_key(int win_id);
void qk_scan(int win_id, char *buffer, int max_len);
int qk_exec(const char *filename);
int qk_read_file(const char *filename, char *buffer);

#endif