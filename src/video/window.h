#ifndef WINDOW_H
#define WINDOW_H

#include "gfx/events.h"
#include "lib/stdint.h"

#define MAX_WINDOWS 10
#define TITLE_BAR_HEIGHT 28
#define WIN_RADIUS 10

#define WIN_TYPE_DEFAULT 0
#define WIN_TYPE_TERMINAL 1

typedef struct {
    int id;
    int type;
    int x;
    int y;
    int w;
    int h;
    char title[32];
    uint32_t bg_color;

    bool is_dragging;
    int drag_offset_x;
    int drag_offset_y;

    char *text_buffer;
    int text_cursor;
    int max_len;

    uint32_t *gfx_buffer;
} Window;

extern Window windows[MAX_WINDOWS];
extern Window *dragging_window;

void window_manager_init();
void corner_mask_init();
Window *window_create(int x, int y, int w, int h, const char *title, int type);
void draw_window_body(int x, int y, int w, int h, uint32_t color, char *text);
void close_window(Window *win);

void draw_windows();
void window_event_handle(Event e);

void window_print(Window *win, const char *str);
void window_clear(Window *win);
char *get_last_line(Window *win);

#endif