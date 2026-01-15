#include "video/window.h"
#include "drivers/GUI/framebuffer.h"
#include "drivers/ps2/mouse.h"
#include "gfx/font_atlas.h"
#include "kernel/shell.h"
#include "mm/kmalloc.h"
#include "util/util.h"
#include "video/compositor.h"
#include "video/texture.h"
#include "lib/stdbool.h"

Window windows[MAX_WINDOWS];
Window *window_stack[MAX_WINDOWS];
int window_count = 0;
Window *dragging_window = 0;

static uint8_t *corner_mask = 0;

Texture *tex_close = 0;
Texture *tex_max = 0;
Texture *tex_min = 0;

void corner_mask_init() {
    corner_mask = corner_mask_texture(WIN_RADIUS);
}

void window_manager_init() {
    corner_mask_init();
    shadows_init(24, WIN_RADIUS, 90);

    window_count = 0;
    dragging_window = 0;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        windows[i].id = 0;
        windows[i].is_dragging = false;
        window_stack[i] = 0;
    }

    tex_close = rhombus_rounded(13, 13, 4, 0xFFD6DBD2);
    tex_max = rhombus_rounded(13, 13, 4, 0xFFD6DBD2);
    tex_min = rhombus_rounded(13, 13, 4, 0xFFD6DBD2);
}

char *get_last_line(Window *win) {
    if (!win || !win->text_buffer)
        return 0;
    if (win->text_cursor == 0)
        return win->text_buffer;

    int i = win->text_cursor - 1;
    while (i >= 0 && win->text_buffer[i] != '\n')
        i--;
    return &win->text_buffer[i + 1];
}

int get_current_line_width(Window *win) {
    int current_w = 0;
    int i = win->text_cursor - 1;

    while (i >= 0 && win->text_buffer[i] != '\n')
        i--;
    i++;

    while (i < win->text_cursor) {
        current_w += get_char_width(win->text_buffer[i]);
        i++;
    }
    return current_w;
}

int get_text_lines_count(Window *win) {
    int lines = 1;
    for (int i = 0; i < win->text_cursor; i++)
        if (win->text_buffer[i] == '\n')
            lines++;
    return lines;
}

void strcpy_custom(char *dest, const char *src) {
    while (*src)
        *dest++ = *src++;
    *dest = 0;
}

void to_front(Window *win) {
    if (!win)
        return;

    int idx = -1;
    for (int i = 0; i < window_count; i++) {
        if (window_stack[i] == win) {
            idx = i;
            break;
        }
    }

    if (idx == -1 || idx == window_count - 1)
        return;

    for (int i = idx; i < window_count - 1; i++)
        window_stack[i] = window_stack[i + 1];

    window_stack[window_count - 1] = win;
    invalidate_screen();
}

Window *window_create(int x, int y, int w, int h, const char *title, int type) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].id == 0) {
            windows[i].id = i + 1;
            windows[i].type = type;
            windows[i].x = x;
            windows[i].y = y;
            windows[i].w = w;
            windows[i].h = h;
            windows[i].bg_color = 0xFF2E2E2E;
            windows[i].is_dragging = false;

            strcpy_custom(windows[i].title, title);

            windows[i].max_len = 4096;
            windows[i].text_buffer = (char *)kmalloc(windows[i].max_len);
            windows[i].text_cursor = 0;
            windows[i].text_buffer[0] = 0;

            int gfx_size = w * (h - TITLE_BAR_HEIGHT) * 4;
            windows[i].gfx_buffer = (uint32_t *)kmalloc(gfx_size);
            if(windows[i].gfx_buffer)
                memset32(windows[i].gfx_buffer, windows[i].bg_color, gfx_size / 4);

            window_stack[window_count++] = &windows[i];
            to_front(&windows[i]);
            invalidate_screen();
            return &windows[i];
        }
    }
    return 0;
}

void close_window(Window *win) {
    if (!win || win->id == 0)
        return;

    int idx = -1;
    for (int i = 0; i < window_count; i++)
        if (window_stack[i] == win) {
            idx = i;
            break;
        }

    if (idx != -1) {
        for (int i = idx; i < window_count - 1; i++)
            window_stack[i] = window_stack[i + 1];
        window_stack[window_count - 1] = 0;
        window_count--;
    }

    if (win->text_buffer) {
        kfree(win->text_buffer);
    }
    if (win->gfx_buffer) {
        kfree(win->gfx_buffer);
    }

    win->id = 0;
    win->text_buffer = 0;
    win->gfx_buffer = 0;

    if (dragging_window == win)
        dragging_window = 0;

    invalidate_screen();
}

void draw_window_body(int x, int y, int w, int h, uint32_t color, char *text) {
    int r = WIN_RADIUS;

    rect_fill(x, y + r, w, h - 2 * r, color);
    rect_fill(x + r, y, w - 2 * r, r, color);
    rect_fill(x + r, y + h - r, w - 2 * r, r, color);

    draw_corner_mask(x, y, r, color, corner_mask, 0);
    draw_corner_mask(x + w - r, y, r, color, corner_mask, 1);
    draw_corner_mask(x, y + h - r, r, color, corner_mask, 2);
    draw_corner_mask(x + w - r, y + h - r, r, color, corner_mask, 3);

    draw_corner_mask(x + w - r, y + h - r, r, color, corner_mask, 3);
}

void draw_window_content(Window *win) {
    if (win->gfx_buffer) {
        int w = win->w;
        int h = win->h - TITLE_BAR_HEIGHT;
        int start_x = win->x;
        int start_y = win->y + TITLE_BAR_HEIGHT;
        
        int r = WIN_RADIUS; 
        int r_sq = r * r; 

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if (x < r && y >= h - r) {
                    int dx = r - 1 - x;
                    int dy = y - (h - r);
                    if (dx * dx + dy * dy >= r_sq) continue;
                }
                
                if (x >= w - r && y >= h - r) {
                    int dx = x - (w - r);
                    int dy = y - (h - r);
                    if (dx * dx + dy * dy >= r_sq) continue;
                }

                uint32_t color = win->gfx_buffer[y * w + x];
                
                if ((color >> 24) != 0) {
                     put_pixel(start_x + x, start_y + y, color);
                }
            }
        }
    }

    if (win->text_buffer) {
        int current_x = win->x + 10;
        int current_y = win->y + 45;
        int start_x = current_x;
        int max_x = win->x + win->w - 10; 
        int max_y = win->y + win->h - 10; 

        char *str = win->text_buffer;
        while (*str) {
            char c = *str;
            if (c == '\n') {
                current_y += get_font_height();
                current_x = start_x;
            } else {
                int w = get_char_width(c);
                
                if (current_x + w < max_x && current_y + get_font_height() < max_y) {
                    current_x = draw_char_vector(current_x, current_y, c, COLOR_WHITE);
                } else {
                    current_x += w;
                }
            }
            str++;
        }
    }
}

void draw_window_head(int x, int y, int w, int h, uint32_t color) {
    int r = WIN_RADIUS;

    rect_fill(x, y + r, w, h - 2 * r, color);
    rect_fill(x + r, y, w - 2 * r, r, color);

    draw_corner_mask(x, y, r, color, corner_mask, 0);
    draw_corner_mask(x + w - r, y, r, color, corner_mask, 1);
}

void draw_window(Window *win, int isActive) {
    uint32_t title_color = isActive ? 0xFF7FB7BE : 0xFFD6DBD2;

    shadow_draw(win->x, win->y, win->w, win->h);
    shadow_draw(win->x, win->y, win->w, win->h);
    draw_window_body(win->x, win->y, win->w, win->h, win->bg_color, win->text_buffer);
    draw_window_content(win);
    draw_window_head(win->x, win->y, win->w, TITLE_BAR_HEIGHT + WIN_RADIUS, 0xFF242622);
    draw_string_vector(win->x + 9, win->y + 20, win->title, title_color);

    int btnY = win->y + 3;
    int closeX = win->x + win->w - 24;
    int maxX = closeX - 20;
    int minX = maxX - 20;

    draw_texture(tex_close, closeX, btnY);
    draw_texture(tex_max, maxX, btnY);
    draw_texture(tex_min, minX, btnY);
}

void draw_windows() {
    for (int i = 0; i < window_count; i++) {
        Window *win = window_stack[i];
        if (win && win->id)
            draw_window(win, i == window_count - 1);
    }
}

void scroll_buffer(Window *win, int max_h) {
    int total_h = get_text_lines_count(win) * get_font_height();

    if (total_h > max_h || win->text_cursor >= win->max_len - 64) {
        int i = 0;
        while (i < win->text_cursor && win->text_buffer[i] != '\n')
            i++;
        if (i < win->text_cursor)
            i++;

        int bytes_to_move = win->text_cursor - i;
        if (bytes_to_move > 0) {
            char *dest = win->text_buffer;
            char *src = win->text_buffer + i;
            for (int k = 0; k < bytes_to_move; k++)
                dest[k] = src[k];

            win->text_cursor = bytes_to_move;
            win->text_buffer[win->text_cursor] = 0;
        } else {

            win->text_cursor = 0;
            win->text_buffer[0] = 0;
        }
    }
}

void window_event_handle(Event e) {
    if (e.type == EVENT_MOUSE_MOVE) {
        if (dragging_window) {
            int new_x = e.x - dragging_window->drag_offset_x;
            int new_y = e.y - dragging_window->drag_offset_y;
            
            int min_x = -(dragging_window->w - 50);
            int max_x = (int)fb_width - 50;          
            int min_y = 0;                  
            int max_y = (int)fb_height - TITLE_BAR_HEIGHT; 
            
            dragging_window->x = (new_x < min_x) ? min_x : (new_x > max_x) ? max_x : new_x;
            dragging_window->y = (new_y < min_y) ? min_y : (new_y > max_y) ? max_y : new_y;
        }
        return;
    }

    if (e.type == EVENT_MOUSE_DOWN && e.button == MOUSE_BTN_LEFT) {
        for (int i = window_count - 1; i >= 0; i--) {
            Window *win = window_stack[i];
            if (!win || !win->id)
                continue;

            if (e.x >= win->x && e.x <= win->x + win->w && e.y >= win->y && e.y <= win->y + win->h) {

                to_front(win);

                int closeX = win->x + win->w - 18;
                int closeY = win->y + 3;

                if (e.x >= closeX && e.x <= closeX + 14 && e.y >= closeY && e.y <= closeY + 14) {
                    close_window(win);
                    return;
                }

                if (e.y <= win->y + TITLE_BAR_HEIGHT) {
                    dragging_window = win;
                    win->is_dragging = true;
                    win->drag_offset_x = e.x - win->x;
                    win->drag_offset_y = e.y - win->y;
                    invalidate_screen();
                }
                return;
            }
        }
    }

    if (e.type == EVENT_MOUSE_UP && e.button == MOUSE_BTN_LEFT) {
        if (dragging_window) {
            dragging_window->is_dragging = false;
            dragging_window = 0;
        }
        return;
    }

    if (e.type == EVENT_KEY_PRESS) {
        dbg_handle_key_count++;
        if (window_count == 0)
            return;

        Window *w = window_stack[window_count - 1];
        uint32_t key = e.key;
        if (key > 255) {
            return;
        }
        char c = (char)key;
        int max_h = w->h - 55 - 10;

        if (c == '\b') {
            if (w->text_cursor > 0) {
                w->text_cursor--;
                w->text_buffer[w->text_cursor] = 0;
                invalidate_window(w->x, w->y, w->w, w->h);
            }
        } else if (c == '\n') {
            if (w->type == WIN_TYPE_TERMINAL) {
                char *cmd = get_last_line(w);
                char cmd_local[64];
                cmd_local[0] = 0;
                
                char *actual_cmd = cmd;
                if (actual_cmd) {
                     while (*actual_cmd && *actual_cmd != '>') {
                        actual_cmd++;
                    }
                    if (*actual_cmd == '>') {
                        actual_cmd++;
                    } else {
                        actual_cmd = cmd;
                    }
                    
                    while (*actual_cmd && *actual_cmd == ' ') {
                        actual_cmd++;
                    }

                    int i = 0;
                    while (*actual_cmd && i < 63) {
                        cmd_local[i++] = *actual_cmd++;
                    }
                    cmd_local[i] = 0;
                }

                if (w->text_cursor < w->max_len - 1) {
                    w->text_buffer[w->text_cursor++] = '\n';
                    w->text_buffer[w->text_cursor] = 0;
                }

                command_run(w, cmd_local);
            } else {
                if (w->text_cursor < w->max_len - 1) {
                    w->text_buffer[w->text_cursor++] = '\n';
                    w->text_buffer[w->text_cursor] = 0;
                }
            }
            scroll_buffer(w, max_h);
            invalidate_window(w->x, w->y, w->w, w->h);
        } else if (key >= 32 && key <= 126) {
            if (w->text_cursor < w->max_len - 2) {
                w->text_buffer[w->text_cursor++] = c;
                w->text_buffer[w->text_cursor] = 0;

                scroll_buffer(w, max_h);
            }
            invalidate_window(w->x, w->y, w->w, w->h);
        }
    }
}

void window_print(Window *win, const char *str) {
    if (!win || !win->text_buffer)
        return;
    int max_h = win->h - 55 - 10;

    while (*str) {
        if (win->text_cursor >= win->max_len - 2) {
            scroll_buffer(win, max_h);
        }
        win->text_buffer[win->text_cursor++] = *str++;
        win->text_buffer[win->text_cursor] = 0;
    }
    scroll_buffer(win, max_h);
    invalidate_window(win->x, win->y, win->w, win->h);
}

void window_clear(Window *win) {
    if (!win || !win->text_buffer)
        return;
    win->text_cursor = 0;
    win->text_buffer[0] = 0;
    invalidate_window(win->x, win->y, win->w, win->h);
}