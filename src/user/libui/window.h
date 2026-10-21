#ifndef WINDOW_H
#define WINDOW_H
#include "abi/events.h"
#include "stdbool.h"
#include "stdint.h"
#include "user/libui/gfx.h"
#define MAX_WINDOWS 32
#define TITLE_BAR_HEIGHT 34
#define WIN_RADIUS 18
#define WIN_CONTENT_GAP 0
#define INPUT_BUFFER_SIZE 256
#define WINDOW_MIN_WIDTH 220
#define WINDOW_MIN_HEIGHT (TITLE_BAR_HEIGHT + 120)
#define WINDOW_COLLAPSED_HEIGHT (TITLE_BAR_HEIGHT + WIN_RADIUS)
#define WIN_FLAG_NO_DECOR  0x01u
#define WIN_FLAG_NO_RADIUS 0x02u
#define UI_SURFACE_COLOR 0xFF1E2127
#define COLOR_WIN_ACTIVE 0xFF9A9A9A
#define COLOR_WIN_INACTIVE 0xFF9A9A9A
#define COLOR_WIN_BG UI_SURFACE_COLOR
#define COLOR_WIN_HEADER 0xFF242424
#define COLOR_WIN_SEP 0xFF9A9A9A
#define COLOR_WHITE 0xFF9A9A9A
typedef struct Window {
    int id;
    int owner_pid;
    int x, y;
    int w, h;
    uint32_t bg_color;
    char title[64];
    char *text_buffer;
    int text_cursor;
    int max_len;
    uint32_t text_color;
    int scroll_y;
    int max_scroll_y;
    bool user_scrolled;
    uint32_t *gfx_buffer;
    bool is_dragging;
    int drag_offset_x;
    int drag_offset_y;
    bool is_collapsed;
    bool is_maximized;
    bool is_resizing;
    int resize_start_w;
    int resize_start_h;
    int resize_origin_x;
    int resize_origin_y;
    int restore_x;
    int restore_y;
    int restore_w;
    int restore_h;
} Window;
extern Window windows[MAX_WINDOWS];
extern Window *window_stack[MAX_WINDOWS];
extern int window_count;
extern Window *dragging_window;
void window_manager_init(void);
void window_manager_set_screen_size(int width, int height);
Window *window_at_point(int x, int y);
void drag_start(uint32_t data_type, const void *data, uint32_t len);
Window *window_create(int owner_pid, int x, int y, int w, int h, const char *title);
void close_window(Window *win);
void draw_windows(gfx_context_t *ctx);
void window_event_handle(Event e);
void window_print(Window *win, const char *str);
void window_clear(Window *win);
void to_front(Window *win);
void window_set_color(Window *win, uint32_t color);
Texture *rhombus_rounded(int w, int h, int r, uint32_t color);
uint8_t *corner_mask_texture(int r);
void draw_rounded_rect(gfx_context_t *ctx, int x, int y, int w, int h, int r, uint32_t color,
                       uint8_t *mask);
int draw_char_vector(gfx_context_t *ctx, int x, int y, char c, uint32_t color);
void gfx_put_pixel(gfx_context_t *ctx, int x, int y, uint32_t color);
void draw_texture(gfx_context_t *ctx, Texture *t, int x, int y);
#endif
