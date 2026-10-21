#include "user/libui/window.h"
#include "kernel/lib/math.h"
#include "stdbool.h"
#include "stdlib.h"
#include "user/libui/font_atlas.h"
#include "user/libui/icons_def.h"
#include "user/libc/libapi.h"
#define RESIZE_HANDLE_SIZE 22
Window windows[MAX_WINDOWS];
Window *window_stack[MAX_WINDOWS];
int window_count = 0;
Window *dragging_window = 0;
static Window *resizing_window = 0;
static int wm_screen_w = 1920;
static int wm_screen_h = 1080;
static uint8_t *corner_mask;
typedef struct wm_gfx_block {
    struct wm_gfx_block *next;
    uint32_t size;
} wm_gfx_block_t;
static wm_gfx_block_t *wm_gfx_free_list = 0;
static uint32_t wm_align16(uint32_t value) {
    return (value + 15) & ~((uint32_t)15);
}
static uint32_t *wm_gfx_alloc(uint32_t size_bytes) {
    if (size_bytes == 0)
        size_bytes = 16;
    size_bytes = wm_align16(size_bytes);
    wm_gfx_block_t **prev = &wm_gfx_free_list;
    wm_gfx_block_t *node = wm_gfx_free_list;
    while (node) {
        if (node->size >= size_bytes) {
            *prev = node->next;
            node->next = 0;
            return (uint32_t *)(node + 1);
        }
        prev = &node->next;
        node = node->next;
    }
    wm_gfx_block_t *fresh = malloc(sizeof(wm_gfx_block_t) + size_bytes);
    if (!fresh)
        return 0;
    fresh->size = size_bytes;
    fresh->next = 0;
    return (uint32_t *)(fresh + 1);
}
static void wm_gfx_free(uint32_t *ptr) {
    if (!ptr)
        return;
    wm_gfx_block_t *hdr = ((wm_gfx_block_t *)ptr) - 1;
    hdr->next = wm_gfx_free_list;
    wm_gfx_free_list = hdr;
}
static Texture *tex_close;
static Texture *tex_max;
static Texture *tex_min;
void window_manager_init(void) {
    corner_mask = corner_mask_texture(WIN_RADIUS);
    tex_close = rhombus_rounded(13, 13, 4, COLOR_WHITE);
    tex_max = rhombus_rounded(13, 13, 4, COLOR_WHITE);
    tex_min = rhombus_rounded(13, 13, 4, COLOR_WHITE);
    window_count = 0;
    dragging_window = 0;
    resizing_window = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        windows[i].id = 0;
        window_stack[i] = 0;
    }
}
void window_manager_set_screen_size(int width, int height) {
    if (width > 0)
        wm_screen_w = width;
    if (height > 0)
        wm_screen_h = height;
}
void to_front(Window *win) {
    int idx = -1;
    for (int i = 0; i < window_count; i++) {
        if (window_stack[i] == win) {
            idx = i;
            break;
        }
    }
    if (idx < 0 || idx == window_count - 1)
        return;
    for (int i = idx; i < window_count - 1; i++)
        window_stack[i] = window_stack[i + 1];
    window_stack[window_count - 1] = win;
}
Window *window_create(int owner_pid, int x, int y, int w, int h, const char *title) {
    if (w < WINDOW_MIN_WIDTH || h < WINDOW_MIN_HEIGHT || w > wm_screen_w ||
        h > wm_screen_h || !title)
        return 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].id)
            continue;
        Window *win = &windows[i];
        win->id = i + 1;
        win->owner_pid = owner_pid;
        win->x = x;
        win->y = y;
        win->w = w;
        win->h = h;
        win->bg_color = COLOR_WIN_BG;
        win->is_collapsed = false;
        win->is_maximized = false;
        win->is_resizing = false;
        win->restore_x = x;
        win->restore_y = y;
        win->restore_w = w;
        win->restore_h = h;
        strncpy(win->title, title, 63);
        win->title[63] = 0;
        win->max_len = 4096;
        win->text_buffer = malloc(win->max_len);
        if (!win->text_buffer) {
            win->id = 0;
            return 0;
        }
        win->text_cursor = 0;
        win->text_buffer[0] = 0;
        int content_h = h - TITLE_BAR_HEIGHT;
        if (content_h < 1)
            content_h = 1;
        uint32_t bytes = (uint32_t)w * (uint32_t)content_h * 4;
        win->gfx_buffer = wm_gfx_alloc(bytes);
        if (!win->gfx_buffer) {
            free(win->text_buffer);
            win->text_buffer = 0;
            win->id = 0;
            return 0;
        }
        memset32(win->gfx_buffer, win->bg_color, w * content_h);
        win->text_color = COLOR_WHITE;
        win->scroll_y = 0;
        win->max_scroll_y = 0;
        win->user_scrolled = false;
        window_stack[window_count++] = win;
        to_front(win);
        return win;
    }
    return 0;
}
void close_window(Window *win) {
    if (!win || !win->id)
        return;
    for (int i = 0; i < window_count; i++) {
        if (window_stack[i] == win) {
            for (int j = i; j < window_count - 1; j++)
                window_stack[j] = window_stack[j + 1];
            window_stack[--window_count] = 0;
            break;
        }
    }
    free(win->text_buffer);
    wm_gfx_free(win->gfx_buffer);
    if (win->owner_pid > 0) {
        sys_window_close(win->owner_pid);
    }
    win->id = 0;
    if (dragging_window == win)
        dragging_window = 0;
    if (resizing_window == win)
        resizing_window = 0;
}
static void draw_corner_mask(gfx_context_t *ctx, int x, int y, int r, uint32_t color, uint8_t *mask,
                             int corner_type) {
    for (int j = 0; j < r; j++) {
        for (int i = 0; i < r; i++) {
            uint8_t a = 0;
            if (corner_type == 0)
                a = mask[j * r + i];
            else if (corner_type == 1)
                a = mask[j * r + (r - 1 - i)];
            else if (corner_type == 2)
                a = mask[(r - 1 - j) * r + i];
            else if (corner_type == 3)
                a = mask[(r - 1 - j) * r + (r - 1 - i)];
            if (a)
                gfx_put_pixel_aa(ctx, x + i, y + j, color, a);
        }
    }
}
static inline int window_visible_height(Window *win) {
    return win->is_collapsed ? WINDOW_COLLAPSED_HEIGHT : win->h;
}
static void window_ensure_in_bounds(Window *win) {
    if (!win)
        return;
    int visible_h = window_visible_height(win);
    if (win->x < 0)
        win->x = 0;
    if (win->y < 0)
        win->y = 0;
    if (win->x + win->w > wm_screen_w)
        win->x = wm_screen_w - win->w;
    if (win->y + visible_h > wm_screen_h)
        win->y = wm_screen_h - visible_h;
    if (win->x < 0)
        win->x = 0;
    if (win->y < 0)
        win->y = 0;
}
static int window_resize_to(Window *win, int new_w, int new_h) {
    if (!win)
        return 0;
    if (new_w < WINDOW_MIN_WIDTH)
        new_w = WINDOW_MIN_WIDTH;
    if (new_h < WINDOW_MIN_HEIGHT)
        new_h = WINDOW_MIN_HEIGHT;
    if (new_w == win->w && new_h == win->h)
        return 1;
    int new_content_h = new_h - TITLE_BAR_HEIGHT;
    if (new_content_h < 1)
        new_content_h = 1;
    uint32_t required_bytes = (uint32_t)new_w * (uint32_t)new_content_h * 4;
    uint32_t *new_buffer = wm_gfx_alloc(required_bytes);
    if (!new_buffer)
        return 0;
    memset32(new_buffer, win->bg_color, new_w * new_content_h);
    if (win->gfx_buffer) {
        int old_content_h = win->h - TITLE_BAR_HEIGHT;
        if (old_content_h < 0)
            old_content_h = 0;
        int copy_h = old_content_h < new_content_h ? old_content_h : new_content_h;
        int copy_w = win->w < new_w ? win->w : new_w;
        for (int row = 0; row < copy_h; row++) {
            memcpy(new_buffer + row * new_w, win->gfx_buffer + row * win->w, copy_w * 4);
        }
        wm_gfx_free(win->gfx_buffer);
    }
    win->gfx_buffer = new_buffer;
    win->w = new_w;
    win->h = new_h;
    if (win->owner_pid > 0) {
        message_t note;
        memset(&note, 0, sizeof(note));
        note.type = MSG_WIN_RESIZED;
        note.data1 = (uint32_t)new_w;
        note.data2 = (uint32_t)(new_h - TITLE_BAR_HEIGHT);
        qk_sys_ipc_send(win->owner_pid, &note);
    }
    return 1;
}
static void window_toggle_collapse(Window *win) {
    if (!win)
        return;
    win->is_collapsed = !win->is_collapsed;
}
static void window_toggle_maximize(Window *win) {
    if (!win)
        return;
    if (win->is_maximized) {
        if (!window_resize_to(win, win->restore_w, win->restore_h))
            return;
        win->x = win->restore_x;
        win->y = win->restore_y;
        win->is_maximized = false;
        window_ensure_in_bounds(win);
        return;
    }
    win->restore_x = win->x;
    win->restore_y = win->y;
    win->restore_w = win->w;
    win->restore_h = win->h;
    win->is_collapsed = false;
    int margin_x = 40;
    int margin_top = 60;
    int margin_bottom = 60;
    if (wm_screen_w < WINDOW_MIN_WIDTH + margin_x * 2)
        margin_x = 0;
    if (wm_screen_h < WINDOW_MIN_HEIGHT + margin_top + margin_bottom) {
        margin_top = 0;
        margin_bottom = 0;
    }
    int new_x = margin_x;
    int new_y = margin_top;
    int new_w = wm_screen_w - margin_x * 2;
    int new_h = wm_screen_h - margin_top - margin_bottom;
    if (!window_resize_to(win, new_w, new_h))
        return;
    win->x = new_x;
    win->y = new_y;
    win->is_maximized = true;
}
static inline bool window_contains_point(Window *win, int px, int py) {
    if (!win)
        return false;
    int visible_h = window_visible_height(win);
    return (px >= win->x && px <= win->x + win->w && py >= win->y && py <= win->y + visible_h);
}
Window *window_at_point(int x, int y) {
    for (int i = window_count - 1; i >= 0; i--) {
        Window *win = window_stack[i];
        if (window_contains_point(win, x, y))
            return win;
    }
    return 0;
}
void drag_start(uint32_t data_type, const void *data, uint32_t len) {
    static int comp_pid_cache = -1;
    if (comp_pid_cache < 0)
        comp_pid_cache = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    if (comp_pid_cache < 0)
        return;
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_DRAG_START;
    msg.data1 = data_type;
    msg.data2 = len < 4096 ? len : 4096;
    if (len > 0)
        memcpy(msg.payload, data, msg.data2);
    qk_sys_ipc_send(comp_pid_cache, &msg);
}
static void window_start_resize(Window *win, int mouse_x, int mouse_y) {
    if (!win)
        return;
    win->is_collapsed = false;
    win->is_maximized = false;
    win->is_resizing = true;
    win->resize_origin_x = mouse_x;
    win->resize_origin_y = mouse_y;
    win->resize_start_w = win->w;
    win->resize_start_h = win->h;
    resizing_window = win;
}
static void window_handle_resize_motion(int mouse_x, int mouse_y) {
    if (!resizing_window)
        return;
    Window *win = resizing_window;
    int delta_w = mouse_x - win->resize_origin_x;
    int delta_h = mouse_y - win->resize_origin_y;
    int target_w = win->resize_start_w + delta_w;
    int target_h = win->resize_start_h + delta_h;
    if (target_w < WINDOW_MIN_WIDTH)
        target_w = WINDOW_MIN_WIDTH;
    if (target_h < WINDOW_MIN_HEIGHT)
        target_h = WINDOW_MIN_HEIGHT;
    if (win->x + target_w > wm_screen_w)
        target_w = wm_screen_w - win->x;
    if (win->y + target_h > wm_screen_h)
        target_h = wm_screen_h - win->y;
    if (!window_resize_to(win, target_w, target_h)) {
        resizing_window->is_resizing = false;
        resizing_window = 0;
    }
}
static void draw_window_body(gfx_context_t *ctx, Window *win) {
    int x = win->x;
    int y = win->y;
    int w = win->w;
    int h = win->is_collapsed ? WINDOW_COLLAPSED_HEIGHT : win->h;
    uint32_t color = win->bg_color;
    int r = WIN_RADIUS;
    gfx_rect_fill(ctx, x, y + r, w, h - 2 * r, color);
    gfx_rect_fill(ctx, x + r, y, w - 2 * r, r, color);
    gfx_rect_fill(ctx, x + r, y + h - r, w - 2 * r, r, color);
    draw_corner_mask(ctx, x, y, r, color, corner_mask, 0);
    draw_corner_mask(ctx, x + w - r, y, r, color, corner_mask, 1);
    draw_corner_mask(ctx, x, y + h - r, r, color, corner_mask, 2);
    draw_corner_mask(ctx, x + w - r, y + h - r, r, color, corner_mask, 3);
}
static void draw_window_content(gfx_context_t *ctx, Window *win) {
    if (win->is_collapsed)
        return;
    if (win->gfx_buffer) {
        int w = win->w;
        int h = win->h - TITLE_BAR_HEIGHT;
        int r = WIN_RADIUS;
        int main_body_h = h - r - WIN_CONTENT_GAP;
        int draw_w = w;
        if (win->x + draw_w > (int)ctx->width)
            draw_w = (int)ctx->width - win->x;
        if (draw_w <= 0)
            return;
        int start_y = win->y + TITLE_BAR_HEIGHT + WIN_CONTENT_GAP;
        int avail_rows = (int)ctx->height - start_y;
        if (avail_rows <= 0)
            return;
        if (main_body_h > avail_rows)
            main_body_h = avail_rows;
        int radius_rows = r;
        if (main_body_h + radius_rows > avail_rows)
            radius_rows = avail_rows - main_body_h;
        uint32_t *src = win->gfx_buffer;
        uint32_t *dst = &ctx->buffer[start_y * ctx->width + win->x];
        for (int i = 0; i < main_body_h; i++) {
            memcpy(dst, src, (uint32_t)draw_w * 4);
            dst += ctx->width;
            src += w;
        }
        for (int i = 0; i < radius_rows; i++) {
            int dy = i;
            for (int j = 0; j < draw_w; j++) {
                bool draw = true;
                if (j < r) {
                    int dx = r - 1 - j;
                    if (dx * dx + dy * dy >= r * r)
                        draw = false;
                } else if (j >= w - r) {
                    int dx = j - (w - r);
                    if (dx * dx + dy * dy >= r * r)
                        draw = false;
                }
                if (draw) {
                    dst[j] = src[j];
                }
            }
            dst += ctx->width;
            src += w;
        }
    }
    if (win->text_buffer) {
        int char_h = get_font_height();
        int margin = 6;
        int max_y = win->h - TITLE_BAR_HEIGHT - 10;
        int max_x = win->w - 20;
        const uint8_t *str = (const uint8_t *)win->text_buffer;
        int line_y = 0;
        int current_x = 0;
        while (*str) {
            if (*str == '\n') {
                line_y += char_h;
                current_x = 0;
                str++;
            } else {
                uint32_t cp;
                int n = utf8_decode(str, &cp);
                current_x += get_codepoint_width(cp);
                if (current_x > max_x) {
                    line_y += char_h;
                    current_x = 0;
                }
                str += n;
            }
        }
        int total_content_h = line_y + char_h;
        win->max_scroll_y = (total_content_h > max_y) ? (total_content_h - max_y) : 0;
        if (strstr(win->title, "Shell") && !win->user_scrolled) {
            win->scroll_y = win->max_scroll_y;
        } else {
            if (win->scroll_y > win->max_scroll_y)
                win->scroll_y = win->max_scroll_y;
        }
        int start_y = win->y + TITLE_BAR_HEIGHT + WIN_CONTENT_GAP + margin;
        const uint8_t *strp = (const uint8_t *)win->text_buffer;
        current_x = win->x + 4;
        line_y = 0;
        while (*strp) {
            if (*strp == '\n') {
                line_y += char_h;
                current_x = win->x + 4;
                strp++;
            } else {
                uint32_t cp;
                int n = utf8_decode(strp, &cp);
                int cw = get_codepoint_width(cp);
                if (current_x + cw > win->x + max_x) {
                    line_y += char_h;
                    current_x = win->x + 4;
                }
                int cy = start_y + line_y - win->scroll_y;
                if (cy >= start_y && cy + char_h <= win->y + win->h - 4) {
                    draw_codepoint_vector(ctx, current_x, cy, cp, win->text_color);
                }
                current_x += cw;
                strp += n;
            }
        }
        if (win->max_scroll_y > 0) {
            int track_h = win->h - TITLE_BAR_HEIGHT - 20;
            int track_x = win->x + win->w - 12;
            int track_y = win->y + TITLE_BAR_HEIGHT + 10;
            gfx_rect_rounded_fill(ctx, track_x, track_y, 8, track_h, 4, 0x33000000);
            int thumb_h = (track_h * max_y) / total_content_h;
            if (thumb_h < 10)
                thumb_h = 10;
            int thumb_y = track_y + (win->scroll_y * (track_h - thumb_h)) /
                                        (win->max_scroll_y ? win->max_scroll_y : 1);
            gfx_rect_rounded_fill(ctx, track_x, thumb_y, 8, thumb_h, 4, 0x66FFFFFF);
        }
    }
}
void draw_window_head(gfx_context_t *ctx, Window *win, bool active) {
    (void)active;
    int x = win->x;
    int y = win->y;
    int w = win->w;
    int r = WIN_RADIUS;
    uint32_t title_color = COLOR_WIN_ACTIVE;
    uint32_t head_bg = COLOR_WIN_HEADER;
    gfx_rect_fill(ctx, x, y + r, w, TITLE_BAR_HEIGHT - r, head_bg);
    gfx_rect_fill(ctx, x + r, y, w - 2 * r, r, head_bg);
    draw_corner_mask(ctx, x, y, r, head_bg, corner_mask, 0);
    draw_corner_mask(ctx, x + w - r, y, r, head_bg, corner_mask, 1);
    int text_y = y + (TITLE_BAR_HEIGHT - 16) / 2 + 14;
    int icon_y = y + (TITLE_BAR_HEIGHT - 18) / 2;
    draw_string_vector(ctx, x + 12, text_y, win->title, title_color);
    int bx = win->x + win->w - 24;
    draw_icon_sized(ctx, gui_icons, bx, icon_y, ICON_CLOSE, COLOR_WHITE, 18);
    draw_icon_sized(ctx, gui_icons, bx - 20, icon_y, ICON_UP, COLOR_WHITE, 18);
    draw_icon_sized(ctx, gui_icons, bx - 40, icon_y, ICON_DOWN, COLOR_WHITE, 18);
}
void draw_windows(gfx_context_t *ctx) {
    wm_screen_w = ctx->width;
    wm_screen_h = ctx->height;
    for (int i = 0; i < window_count; i++) {
        Window *win = window_stack[i];
        bool active = (i == window_count - 1);
        int visible_h = win->is_collapsed ? WINDOW_COLLAPSED_HEIGHT : win->h;
        gfx_draw_shadow(ctx, win->x, win->y, win->w, visible_h, WIN_RADIUS, 10);
        draw_window_body(ctx, win);
        draw_window_content(ctx, win);
        draw_window_head(ctx, win, active);
    }
}
void window_event_handle(Event e) {
    if (e.type == EVENT_MOUSE_MOVE) {
        if (resizing_window && resizing_window->is_resizing) {
            window_handle_resize_motion(e.x, e.y);
            return;
        }
        if (dragging_window) {
            dragging_window->x = e.x - dragging_window->drag_offset_x;
            dragging_window->y = e.y - dragging_window->drag_offset_y;
            window_ensure_in_bounds(dragging_window);
            return;
        }
    }
    if (e.type == EVENT_MOUSE_DOWN && e.button == MOUSE_BTN_LEFT) {
        for (int i = window_count - 1; i >= 0; i--) {
            Window *win = window_stack[i];
            if (!window_contains_point(win, e.x, e.y))
                continue;
            to_front(win);
            int button_top = win->y + 6;
            int button_bottom = button_top + 18;
            int close_x0 = win->x + win->w - 24;
            int close_x1 = close_x0 + 16;
            int max_x0 = close_x0 - 20;
            int max_x1 = max_x0 + 16;
            int min_x0 = max_x0 - 20;
            int min_x1 = min_x0 + 16;
            if (e.y >= button_top && e.y <= button_bottom) {
                if (e.x >= close_x0 && e.x <= close_x1) {
                    close_window(win);
                    return;
                }
                if (e.x >= max_x0 && e.x <= max_x1) {
                    window_toggle_maximize(win);
                    return;
                }
                if (e.x >= min_x0 && e.x <= min_x1) {
                    window_toggle_collapse(win);
                    return;
                }
            }
            int visible_h = window_visible_height(win);
            if (!win->is_collapsed && !win->is_maximized) {
                int resize_x0 = win->x + win->w - RESIZE_HANDLE_SIZE;
                int resize_y0 = win->y + visible_h - RESIZE_HANDLE_SIZE;
                if (e.x >= resize_x0 && e.y >= resize_y0) {
                    window_start_resize(win, e.x, e.y);
                    return;
                }
            }
            if (e.y <= win->y + TITLE_BAR_HEIGHT) {
                int drag_px = e.x - win->x;
                if (drag_px < 0)
                    drag_px = 0;
                if (drag_px > win->w)
                    drag_px = win->w;
                if (win->is_maximized) {
                    int prev_w = win->w;
                    int rel = drag_px;
                    window_toggle_maximize(win);
                    int denom = (prev_w == 0) ? 1 : prev_w;
                    win->drag_offset_x = (rel * win->w) / denom;
                } else {
                    win->drag_offset_x = drag_px;
                }
                int drag_py = e.y - win->y;
                if (drag_py < 0)
                    drag_py = TITLE_BAR_HEIGHT / 2;
                win->drag_offset_y = drag_py;
                dragging_window = win;
                return;
            }
            if (win->max_scroll_y > 0) {
                int track_x = win->x + win->w - 12;
                if (e.x >= track_x && e.x <= track_x + 8) {
                    int track_y = win->y + TITLE_BAR_HEIGHT + 10;
                    int track_h = win->h - TITLE_BAR_HEIGHT - 20;
                    int relative_y = e.y - track_y;
                    if (relative_y >= 0 && relative_y <= track_h) {
                        win->scroll_y = (relative_y * win->max_scroll_y) / track_h;
                        return;
                    }
                }
            }
            return;
        }
    }
    if (e.type == EVENT_MOUSE_UP && e.button == MOUSE_BTN_LEFT) {
        dragging_window = 0;
        if (resizing_window) {
            resizing_window->is_resizing = false;
            resizing_window = 0;
        }
    }
    if (e.type == EVENT_MOUSE_SCROLL) {
        for (int i = window_count - 1; i >= 0; i--) {
            Window *win = window_stack[i];
            if (!window_contains_point(win, e.x, e.y))
                continue;
            if (win->is_collapsed)
                break;
            int step = e.scroll * 20;
            win->scroll_y += step;
            if (win->scroll_y < 0)
                win->scroll_y = 0;
            if (win->scroll_y > win->max_scroll_y)
                win->scroll_y = win->max_scroll_y;
            win->user_scrolled = (win->scroll_y < win->max_scroll_y);
            break;
        }
    }
}
void window_print(Window *win, const char *str) {
    while (*str) {
        if (*str == '\b') {
            if (win->text_cursor > 0) {
                win->text_cursor--;
                win->text_buffer[win->text_cursor] = 0;
            }
        } else {
            if (win->text_cursor < win->max_len - 1) {
                win->text_buffer[win->text_cursor++] = *str;
            }
        }
        str++;
    }
    win->text_buffer[win->text_cursor] = 0;
}
void window_clear(Window *win) {
    win->text_cursor = 0;
    win->text_buffer[0] = 0;
    if (win->gfx_buffer) {
        int content_h = win->h - TITLE_BAR_HEIGHT;
        if (content_h < 0)
            content_h = 0;
        int total = win->w * content_h;
        if (total > 0)
            memset32(win->gfx_buffer, win->bg_color, total);
    }
}
void window_set_color(Window *win, uint32_t color) {
    if (win)
        win->text_color = color;
}
