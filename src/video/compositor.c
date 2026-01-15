#include "video/compositor.h"
#include "drivers/GUI/framebuffer.h"
#include "drivers/ps2/mouse.h"
#include "drivers/rtc.h"
#include "drivers/timer.h"
#include "gfx/bitmaps.h"
#include "gfx/events.h"
#include "gfx/font_atlas.h"
#include "lib/stdlib.h"
#include "video/texture.h"
#include "video/window.h"

static int frames = 0;
static int fps = 0;
static uint64_t last_time = 0;
static int prev_drawn_fps = -1;

extern int counter;
static int prev_counter = -1;
extern volatile int needs_redraw;
extern Time sys_time;
static Time prev_time;
extern volatile int needs_redraw;

static int32_t prev_mx = 0;
static int32_t prev_my = 0;
static Window *prev_drag_win = 0;
static int prev_win_x = 0;
static int prev_win_y = 0;

static int force_redraw = 1;
static Texture *current_wallpaper = 0;

#define MAX_DIRTY_RECTS 32

typedef struct {
    int x;
    int y;
    int w;
    int h;
} DirtyRect;

static DirtyRect dirty_rects[MAX_DIRTY_RECTS];
static int dirty_count = 0;

static inline int min_i(int a, int b) {
    return (a < b) ? a : b;
}
static inline int max_i(int a, int b) {
    return (a > b) ? a : b;
}
static inline int clamp(int val, int min, int max) {
    if (val < min)
        return min;
    if (val > max)
        return max;
    return val;
}

void set_background(Texture *tex) {
    current_wallpaper = tex;
    force_redraw = 1;
}

void compositor_init() {
    prev_mx = mouse_x;
    prev_my = mouse_y;
    prev_counter = counter;
    force_redraw = 1;
    last_time = get_uptime_ms();
    prev_time.second = 99;
}

void invalidate_screen() {
    force_redraw = 1;
    dirty_count = 0;
    needs_redraw = 1;
}

static int rects_overlap(DirtyRect *a, int x, int y, int w, int h) {
    int ax2 = a->x + a->w;
    int ay2 = a->y + a->h;
    int bx2 = x + w;
    int by2 = y + h;

    if (ax2 < x || bx2 < a->x)
        return 0;
    if (ay2 < y || by2 < a->y)
        return 0;
    return 1;
}

static void merge_rect(DirtyRect *dst, int x, int y, int w, int h) {
    int nx1 = min_i(dst->x, x);
    int ny1 = min_i(dst->y, y);
    int nx2 = max_i(dst->x + dst->w, x + w);
    int ny2 = max_i(dst->y + dst->h, y + h);

    dst->x = nx1;
    dst->y = ny1;
    dst->w = nx2 - nx1;
    dst->h = ny2 - ny1;
}

static void add_dirty_rect(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0)
        return;

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }

    if (x >= (int)fb_width || y >= (int)fb_height)
        return;

    if (x + w > (int)fb_width)
        w = (int)fb_width - x;
    if (y + h > (int)fb_height)
        h = (int)fb_height - y;

    for (int i = 0; i < dirty_count; i++) {
        if (rects_overlap(&dirty_rects[i], x, y, w, h)) {
            merge_rect(&dirty_rects[i], x, y, w, h);
            return;
        }
    }

    if (dirty_count < MAX_DIRTY_RECTS) {
        dirty_rects[dirty_count].x = x;
        dirty_rects[dirty_count].y = y;
        dirty_rects[dirty_count].w = w;
        dirty_rects[dirty_count].h = h;
        dirty_count++;
    } else {
        force_redraw = 1;
        dirty_count = 0;
    }
}

void invalidate_window(int x, int y, int w, int h) {
    int margin = WIN_SHADOW_SIZE;
    add_dirty_rect(x - margin, y - margin, w + margin * 2, h + margin * 2);
    needs_redraw = 1;
}

void safe_swap(int x, int y, int w, int h) {
    int pad = 4;
    int x1 = clamp(x - pad, 0, fb_width);
    int y1 = clamp(y - pad, 0, fb_height);
    int x2 = clamp(x + w + pad, 0, fb_width);
    int y2 = clamp(y + h + pad, 0, fb_height);

    int new_w = x2 - x1;
    int new_h = y2 - y1;

    if (new_w > 0 && new_h > 0) {
        swap_rect(x1, y1, new_w, new_h);
    }
}

void compositor_paint() {
    uint64_t now = get_uptime_ms();
    if (now >= last_time + 1000) {
        fps = frames;
        frames = 0;
        last_time = now;
    }

    int32_t cur_mx = mouse_x;
    int32_t cur_my = mouse_y;
    int moved = 0;

    if (cur_mx != prev_mx || cur_my != prev_my)
        moved = 1;
    if (prev_drag_win)
        moved = 1;
    if (dragging_window)
        moved = 1;
    if (counter != prev_counter)
        moved = 1;
    if (fps != prev_drawn_fps)
        moved = 1;
    if (sys_time.second != prev_time.second)
        moved = 1;

    if (dirty_count > 0)
        moved = 1;

    if (!moved && !force_redraw)
        return;

    frames++;

    if (current_wallpaper) {
        draw_texture(current_wallpaper, 0, 0);
    } else {
        clear_screen(0xFF1C1D1B);
    }

    char time_buf[16];
    char num_buf[4];
    time_to_str(sys_time.hour, num_buf);
    time_buf[0] = num_buf[0];
    time_buf[1] = num_buf[1];
    time_buf[2] = ':';
    time_to_str(sys_time.minute, num_buf);
    time_buf[3] = num_buf[0];
    time_buf[4] = num_buf[1];
    time_buf[5] = ':';
    time_to_str(sys_time.second, num_buf);
    time_buf[6] = num_buf[0];
    time_buf[7] = num_buf[1];
    time_buf[8] = 0;
    draw_string_vector(35, 30, time_buf, 0xFFFFFFFF);

    int fps_x = fb_width - 120;
    int fps_y = 20;
    char fps_str[32];
    int_to_str(fps, fps_str, 0);
    draw_string_vector(fps_x + 10, fps_y + 10, "FPS:", 0xFFAAAAAA);
    draw_string_vector(fps_x + 55, fps_y + 10, fps_str, 0xFF00FF00);

    draw_windows();
    draw_sprite(cur_mx, cur_my, CURSOR_W, CURSOR_H, cursor, cursor_palette);

    if (force_redraw) {
        wait_vblank();
        swap_buffers();
        force_redraw = 0;
        prev_drawn_fps = fps;
        dirty_count = 0;
    } else {
        if (dirty_count > 0) {
            for (int i = 0; i < dirty_count; i++) {
                DirtyRect *rect = &dirty_rects[i];
                safe_swap(rect->x, rect->y, rect->w, rect->h);
            }
            dirty_count = 0;
        }

        int margin = WIN_SHADOW_SIZE;

        if (sys_time.second != prev_time.second)
            safe_swap(20, 20, 150, 40);
        if (counter != prev_counter)
            safe_swap(20, 20, 150, 40);
        if (fps != prev_drawn_fps) {
            safe_swap(fps_x, fps_y, 100, 40);
            prev_drawn_fps = fps;
        }

        if (prev_drag_win) {
            safe_swap(
                prev_win_x - margin, prev_win_y - margin, prev_drag_win->w + margin * 2, prev_drag_win->h + margin * 2);
        }

        if (dragging_window) {
            safe_swap(dragging_window->x - margin, dragging_window->y - margin, dragging_window->w + margin * 2,
                dragging_window->h + margin * 2);
        }

        if (prev_drag_win || dragging_window) {
            wait_vblank();
        }

        int m_x1 = min_i(prev_mx, cur_mx);
        int m_y1 = min_i(prev_my, cur_my);
        int m_x2 = max_i(prev_mx, cur_mx) + CURSOR_W;
        int m_y2 = max_i(prev_my, cur_my) + CURSOR_H;

        safe_swap(m_x1, m_y1, m_x2 - m_x1, m_y2 - m_y1);
    }

    prev_mx = cur_mx;
    prev_my = cur_my;
    prev_counter = counter;
    prev_time = sys_time;

    if (dragging_window) {
        prev_drag_win = dragging_window;
        prev_win_x = dragging_window->x;
        prev_win_y = dragging_window->y;
    } else {
        prev_drag_win = 0;
    }
}
