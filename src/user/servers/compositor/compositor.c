#include "drivers/video/bitmaps.h"
#include "user/servers/compositor/output.h"
#include "user/servers/compositor/desktop_shell.h"
#include "abi/events.h"
#include "stdlib.h"
#include "user/libui/font_atlas.h"
#include "user/libui/gfx.h"
#include "user/libui/icons_def.h"
#include "user/libc/libapi.h"
#include "user/libc/path.h"
#include "user/libui/window.h"
#define _STDDEF_H
#define _STDDEF_H_
#define _ANSI_STDDEF_H
#define _GCC_WRAP_STDINT_H
#define _STDINT_H
#define _SYS_STDINT_H_
#ifndef NULL
#define NULL ((void *)0)
#endif
#define STBI_MALLOC(sz) malloc((unsigned int)(sz))
#define STBI_FREE(p) free((p))
#define STBI_REALLOC_SIZED(p, o, n) realloc((p), (unsigned int)(n))
#define STBI_REALLOC(p, sz) realloc((p), (unsigned int)(sz))
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_TGA
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_ASSERT(x) ((void)0)
#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"
fb_info_t fb;
gfx_context_t ctx;
int32_t mx = 512, my = 384;
uint32_t fps_counter = 0, last_fps_update = 0;
static uint8_t *wallpaper_bmp;
static uint32_t wallpaper_size;
static uint8_t *wallpaper_rgba;
static int wallpaper_width, wallpaper_height;
int current_fps = 0;
static int debug_overlay_visible = 0;
static uint32_t total_ipc_messages = 0;

#define CLIP_MAX 4095
static char g_clipboard[CLIP_MAX + 1];
static int g_clip_len = 0;
typedef enum {
    POWER_ACTION_SHUTDOWN = 0,
    POWER_ACTION_REBOOT,
    POWER_ACTION_SLEEP
} power_action_type_t;
typedef struct {
    const char *label;
    int icon_id;
    power_action_type_t type;
    ui_rect_t bounds;
} power_action_t;
#define POWER_ACTION_COUNT 3
#define POWER_MENU_WIDTH 125
#define POWER_MENU_ITEM_H 40
#define POWER_MENU_RADIUS 9
#define POWER_ICON_SCALE 1
#define POWER_MENU_UP_OFFSET 8
ui_rect_t power_button_rect = {0, 0, 0, 0};
static int power_overlay_visible = 0;
static int power_overlay_hover = -1;
static int sleep_mode_active = 0;
static power_action_t power_actions[POWER_ACTION_COUNT] = {
    {"Shutdown", ICON_SHUTDOWN, POWER_ACTION_SHUTDOWN, {0, 0, 0, 0}},
    {"Reboot", ICON_UP, POWER_ACTION_REBOOT, {0, 0, 0, 0}},
    {"Sleep", ICON_SUN, POWER_ACTION_SLEEP, {0, 0, 0, 0}}};
static uint32_t *blur_buffer = 0;
static int blur_buf_w = 0;
static int blur_buf_h = 0;
static uint32_t *blur_snapshot = 0;
static int blur_snapshot_w = 0;
static int blur_snapshot_h = 0;
static int blur_snapshot_valid = 0;
static int storage_server_pid = -1;
static uint32_t current_time_ms = 0;
static uint32_t desktop_notification_until = 0;
static char desktop_notification_text[64];
typedef struct {
    int active;
    int source_pid;
    int target_pid;
    uint32_t data_type;
    uint32_t data_len;
    uint8_t data[4096];
} drag_session_t;
static drag_session_t g_drag;
#define DESKTOP_NOTIFICATION_MS 3200

#ifndef ICON_IMAGE
#define ICON_IMAGE 0xEA06
#endif
static void process_message(message_t *msg);
static inline int clamp_int(int v, int min, int max) {
    if (v < min)
        return min;
    if (v > max)
        return max;
    return v;
}
static inline int abs_int(int v) {
    return (v < 0) ? -v : v;
}
static int ensure_storage_pid(void) {
    if (storage_server_pid < 0)
        storage_server_pid = qk_get_server_pid(QK_SERVER_STORAGE);
    return (storage_server_pid < 0) ? -1 : 0;
}
void compositor_notify(const char *text) {
    if (!text)
        return;
    strncpy(desktop_notification_text, text, sizeof(desktop_notification_text) - 1);
    desktop_notification_until = qk_get_uptime() + DESKTOP_NOTIFICATION_MS;
}
static int wait_for_storage_reply(message_t *resp) {
    if (!resp)
        return -1;
    message_t msg;
    while (1) {
        if (qk_ipc_recv(&msg) != 0)
            continue;
        if (msg.sender_pid == storage_server_pid) {
            memcpy(resp, &msg, sizeof(message_t));
            return 0;
        }
        process_message(&msg);
    }
}
static void *load_file_from_storage(const char *filename, uint32_t *out_size) {
    if (out_size)
        *out_size = 0;
    if (!filename || ensure_storage_pid() != 0)
        return 0;
    message_t req, resp;
    memset(&req, 0, sizeof(message_t));
    req.type = MSG_FILE_OPEN;
    req.data1 = 0xFFFFu;
    strncpy((char *)req.payload, filename, sizeof(req.payload) - 1);
    if (qk_ipc_send_recv(storage_server_pid, &req, &resp) != 0)
        return 0;
    if (resp.type != MSG_FILE_SIZE || resp.data1 == 0)
        return 0;
    uint32_t size = resp.data1;
    if (out_size)
        *out_size = size;
    uint32_t page_count = resp.data2;
    uint32_t page_offset = resp.data3;
    if (page_count > 0 && page_count <= 4096) {
        static uint32_t pages[4096];
        uint32_t fetched = page_count < 128 ? page_count : 128;
        memcpy(pages, resp.payload, fetched * sizeof(uint32_t));
        while (fetched < page_count) {
            memset(&req, 0, sizeof(req));
            req.type = MSG_FILE_GET_PAGES;
            req.data1 = fetched;
            if (qk_ipc_send_recv(storage_server_pid, &req, &resp) != 0 ||
                resp.type != MSG_FILE_SIZE || resp.data1 == 0)
                break;
            uint32_t got = resp.data1;
            if (got > page_count - fetched) got = page_count - fetched;
            memcpy(pages + fetched, resp.payload, got * sizeof(uint32_t));
            fetched += got;
        }
        if (fetched == page_count) {
            uint32_t mapped = qk_map_phys_list(pages, page_count);
            if (mapped) {
                uint8_t *copy = (uint8_t *)malloc(size);
                if (!copy) return 0;
                memcpy(copy, (void *)(uintptr_t)(mapped + page_offset), size);
                return copy;
            }
        }
    }
    uint8_t *buffer = (uint8_t *)malloc(size);
    if (!buffer)
        return 0;
    uint32_t offset = 0;
    while (offset < size) {
        req.type = MSG_REQ_DATA;
        req.data1 = offset;
        req.data2 = 4088;
        if (qk_sys_ipc_send(storage_server_pid, &req) != 0)
            break;
        if (wait_for_storage_reply(&resp) != 0)
            break;
        if (resp.type != MSG_FILE_DATA)
            break;
        uint32_t chunk = resp.data1;
        if (chunk == 0)
            break;
        if (chunk > sizeof(resp.payload))
            chunk = sizeof(resp.payload);
        if (offset + chunk > size)
            chunk = size - offset;
        memcpy(buffer + offset, resp.payload, chunk);
        offset += chunk;
    }
    if (offset != size) {
        free(buffer);
        return 0;
    }
    return buffer;
}

void *compositor_load_file(const char *path, uint32_t *size) {
    return load_file_from_storage(path, size);
}

static int desk_icon_id(const char *name) {
    int len = strlen(name);
    if (len >= 4) {
        char ext[4];
        for (int i = 0; i < 3; i++) {
            ext[i] = name[len - 3 + i];
            if (ext[i] >= 'a' && ext[i] <= 'z')
                ext[i] -= 32;
        }
        ext[3] = 0;
        if (ext[0] == 'B' && ext[1] == 'M' && ext[2] == 'P')
            return ICON_IMAGE;
        if (ext[0] == 'P' && ext[1] == 'N' && ext[2] == 'G')
            return ICON_IMAGE;
        if (ext[0] == 'J' && ext[1] == 'P' && ext[2] == 'G')
            return ICON_IMAGE;
        if (ext[0] == 'T' && ext[1] == 'X' && ext[2] == 'T')
            return ICON_FILE_TEXT;
        if (ext[0] == 'Q' && ext[1] == 'E' && ext[2] == 'X')
            return ICON_BINARY;
    }
    return ICON_FILE_TEXT;
}

static void ensure_blur_buffer(int w, int h) {
    if (blur_buffer && blur_buf_w == w && blur_buf_h == h)
        return;
    if (blur_buffer)
        free(blur_buffer);
    blur_buffer = (uint32_t *)malloc(sizeof(uint32_t) * w * h);
    blur_buf_w = w;
    blur_buf_h = h;
}
static void ensure_blur_snapshot(int w, int h) {
    if (blur_snapshot && blur_snapshot_w == w && blur_snapshot_h == h)
        return;
    if (blur_snapshot)
        free(blur_snapshot);
    blur_snapshot = (uint32_t *)malloc(sizeof(uint32_t) * w * h);
    blur_snapshot_w = w;
    blur_snapshot_h = h;
}
static void invalidate_blur_snapshot(void) {
    blur_snapshot_valid = 0;
}
static const int GAUSS_RADIUS = 4;
static const int gauss_kernel[9] = {1, 8, 28, 56, 70, 56, 28, 8, 1};
static const int gauss_weight_sum = 256;
static void blur_horizontal(uint32_t *src, uint32_t *dst, int w, int h) {
    for (int y = 0; y < h; y++) {
        int base = y * w;
        for (int x = 0; x < w; x++) {
            int sum_r = 0, sum_g = 0, sum_b = 0;
            for (int k = -GAUSS_RADIUS; k <= GAUSS_RADIUS; k++) {
                int sx = clamp_int(x + k, 0, w - 1);
                uint32_t px = src[base + sx];
                int weight = gauss_kernel[k + GAUSS_RADIUS];
                sum_r += ((px >> 16) & 0xFF) * weight;
                sum_g += ((px >> 8) & 0xFF) * weight;
                sum_b += (px & 0xFF) * weight;
            }
            dst[base + x] = 0xFF000000 | ((sum_r / gauss_weight_sum) << 16) |
                            ((sum_g / gauss_weight_sum) << 8) | (sum_b / gauss_weight_sum);
        }
    }
}
static void blur_vertical(uint32_t *src, uint32_t *dst, int w, int h) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int sum_r = 0, sum_g = 0, sum_b = 0;
            for (int k = -GAUSS_RADIUS; k <= GAUSS_RADIUS; k++) {
                int sy = clamp_int(y + k, 0, h - 1);
                uint32_t px = src[sy * w + x];
                int weight = gauss_kernel[k + GAUSS_RADIUS];
                sum_r += ((px >> 16) & 0xFF) * weight;
                sum_g += ((px >> 8) & 0xFF) * weight;
                sum_b += (px & 0xFF) * weight;
            }
            dst[y * w + x] = 0xFF000000 | ((sum_r / gauss_weight_sum) << 16) |
                             ((sum_g / gauss_weight_sum) << 8) | (sum_b / gauss_weight_sum);
        }
    }
}
static void blur_buffer_inplace(uint32_t *buffer, int w, int h, int intensity) {
    if (intensity <= 0)
        return;
    ensure_blur_buffer(w, h);
    int passes = clamp_int(intensity / 6 + 1, 1, 4);
    for (int i = 0; i < passes; i++) {
        blur_horizontal(buffer, blur_buffer, w, h);
        blur_vertical(blur_buffer, buffer, w, h);
    }
}
static int point_in_rect(ui_rect_t rect, int px, int py) {
    return (px >= rect.x && px < rect.x + rect.w && py >= rect.y && py < rect.y + rect.h);
}
static void layout_power_menu(void) {
    if (!ctx.width || !ctx.height)
        return;
    int x = power_button_rect.x + power_button_rect.w - POWER_MENU_WIDTH;
    int y = power_button_rect.y + power_button_rect.h + POWER_MENU_UP_OFFSET;
    for (int i = 0; i < POWER_ACTION_COUNT; i++) {
        power_actions[i].bounds.x = x + 5;
        power_actions[i].bounds.y = y + 5 + i * POWER_MENU_ITEM_H;
        power_actions[i].bounds.w = POWER_MENU_WIDTH - 10;
        power_actions[i].bounds.h = POWER_MENU_ITEM_H;
    }
}
static int find_power_action_at(int x, int y) {
    for (int i = 0; i < POWER_ACTION_COUNT; i++) {
        if (point_in_rect(power_actions[i].bounds, x, y))
            return i;
    }
    return -1;
}
static void close_power_overlay(void) {
    power_overlay_visible = 0;
    power_overlay_hover = -1;
    invalidate_blur_snapshot();
}
static void open_power_overlay(void) {
    power_overlay_visible = 1;
    power_overlay_hover = -1;
    layout_power_menu();
    invalidate_blur_snapshot();
}
static void exit_sleep_mode(void) {
    sleep_mode_active = 0;
    invalidate_blur_snapshot();
}
static void enter_sleep_mode(void) {
    sleep_mode_active = 1;
    close_power_overlay();
    invalidate_blur_snapshot();
}
static void trigger_power_action(power_action_type_t type) {
    switch (type) {
    case POWER_ACTION_SHUTDOWN:
        qk_power_shutdown();
        break;
    case POWER_ACTION_REBOOT:
        qk_power_reboot();
        break;
    case POWER_ACTION_SLEEP:
        enter_sleep_mode();
        break;
    }
}
static int handle_mouse_for_ui(Event *e) {
    if (sleep_mode_active) {
        if (e->type == EVENT_MOUSE_DOWN || e->type == EVENT_MOUSE_MOVE || e->type == EVENT_MOUSE_UP)
            exit_sleep_mode();
        return 1;
    }
    if (power_overlay_visible) {
        if (e->type == EVENT_MOUSE_MOVE) {
            power_overlay_hover = find_power_action_at(e->x, e->y);
            return 1;
        }
        if (e->type == EVENT_MOUSE_DOWN && e->button == MOUSE_BTN_LEFT) {
            int idx = find_power_action_at(e->x, e->y);
            if (idx >= 0)
                trigger_power_action(power_actions[idx].type);
            else
                close_power_overlay();
            return 1;
        }
        if (e->type == EVENT_MOUSE_UP)
            return 1;
    }
    if (e->type == EVENT_MOUSE_DOWN && e->button == MOUSE_BTN_LEFT) {
        if (point_in_rect(power_button_rect, e->x, e->y)) {
            if (power_overlay_visible)
                close_power_overlay();
            else
                open_power_overlay();
            return 1;
        }
    }
    return 0;
}

void render_power_overlay(void) {
    if (!power_overlay_visible)
        return;
    layout_power_menu();
    int menu_h = POWER_ACTION_COUNT * POWER_MENU_ITEM_H + 10;
    int x = power_button_rect.x + power_button_rect.w - POWER_MENU_WIDTH;
    int y = power_button_rect.y + power_button_rect.h + POWER_MENU_UP_OFFSET;
    gfx_draw_shadow(&ctx, x, y, POWER_MENU_WIDTH, menu_h, POWER_MENU_RADIUS, 12);
    gfx_rect_rounded_fill(&ctx, x, y, POWER_MENU_WIDTH, menu_h, POWER_MENU_RADIUS,
                          UI_SURFACE_COLOR);
    for (int i = 0; i < POWER_ACTION_COUNT; i++) {
        ui_rect_t r = power_actions[i].bounds;
        if (i == power_overlay_hover)
            gfx_rect_rounded_fill(&ctx, r.x, r.y, r.w, r.h, 8, 0x33000000);
        draw_icon_sized(&ctx, gui_icons, r.x + 7, r.y + (r.h - 18) / 2, power_actions[i].icon_id,
                        COLOR_WIN_ACTIVE, 18);
        draw_string_vector(&ctx, r.x + 36, r.y + r.h / 2 - 8, power_actions[i].label,
                           COLOR_WIN_ACTIVE);
    }
}
void render_sleep_screen(void) {
    if (!sleep_mode_active)
        return;
    gfx_rect_fill(&ctx, 0, 0, (int)ctx.width, (int)ctx.height, 0xCC000000);
    const char *title = "Sleeping...";
    const char *hint = "Press any key or click to wake";
    int cx = (int)ctx.width / 2;
    draw_string_vector(&ctx, cx - (int)strlen(title) * 6, (int)ctx.height / 2 - 10, title,
                       COLOR_WIN_ACTIVE);
    draw_string_vector(&ctx, cx - (int)strlen(hint) * 4, (int)ctx.height / 2 + 20, hint,
                       COLOR_WIN_ACTIVE);
}
static void render_desktop_notification(void) {
    if (!desktop_notification_until || current_time_ms >= desktop_notification_until ||
        !desktop_notification_text[0])
        return;
    int padding = 20;
    int h = 42;
    int text_w = (int)strlen(desktop_notification_text) * 8;
    int w = text_w + padding * 2;
    if (w < 160)
        w = 160;
    int x = 60;
    int y = (int)ctx.height - h - 60;
    gfx_rect_rounded_fill(&ctx, x, y, w, h, 16, 0xAA000000);
    draw_string_vector(&ctx, x + padding, y + 12, desktop_notification_text, COLOR_WIN_ACTIVE);
}
void render_debug_overlay(void) {
    if (!debug_overlay_visible)
        return;
    int x = 20;
    int y = 60;
    uint32_t used_mem, total_mem, cpu_load;
    qk_get_stats(&used_mem, &total_mem, &cpu_load);
    uint32_t uptime = qk_get_uptime();
    process_info_t procs[32];
    int task_count = qk_get_procs(procs, 32);
    char buf[64];
    char val[16];
    draw_string_vector(&ctx, x, y, "--- SYSTEM DEBUG ---", 0xFFFFFFFF);
    memset(buf, 0, 64);
    strcat(buf, "CPU Load: ");
    int_to_str(cpu_load, val, 10);
    strcat(buf, val);
    strcat(buf, "%");
    draw_string_vector(&ctx, x, y + 25, buf, 0xFF55FF55);
    memset(buf, 0, 64);
    strcat(buf, "Memory: ");
    int_to_str(used_mem / 1024 / 1024, val, 10);
    strcat(buf, val);
    strcat(buf, " MB used");
    draw_string_vector(&ctx, x, y + 45, buf, 0xFF55FFFF);
    memset(buf, 0, 64);
    strcat(buf, "Uptime: ");
    int_to_str(uptime / 1000, val, 10);
    strcat(buf, val);
    strcat(buf, "s");
    draw_string_vector(&ctx, x, y + 65, buf, 0xFFFFFF55);
    memset(buf, 0, 64);
    strcat(buf, "Tasks: ");
    int_to_str(task_count, val, 10);
    strcat(buf, val);
    draw_string_vector(&ctx, x, y + 85, buf, 0xFFFF55FF);
    memset(buf, 0, 64);
    strcat(buf, "IPC Events: ");
    int_to_str(total_ipc_messages, val, 10);
    strcat(buf, val);
    draw_string_vector(&ctx, x, y + 105, buf, 0xFFABB2BF);
}
static void process_message(message_t *msg) {
    total_ipc_messages++;
    if (msg->type == MSG_KEY_EVENT || msg->type == MSG_MOUSE_EVENT) {
        Event *e = (Event *)msg->payload;
        if (e->type == EVENT_KEY_PRESS && msg->data1 == 0x58) {
            debug_overlay_visible = !debug_overlay_visible;
            return;
        }
        if (e->type == EVENT_KEY_PRESS || e->type == EVENT_KEY_RELEASE) {
            if (sleep_mode_active) {
                if (e->type == EVENT_KEY_PRESS)
                    exit_sleep_mode();
                return;
            }
            if (power_overlay_visible) {
                if (e->type == EVENT_KEY_PRESS && e->key == 0x01)
                    close_power_overlay();
                return;
            }
        }
        int consumed = 0;
        if (e->type == EVENT_MOUSE_MOVE || e->type == EVENT_MOUSE_DOWN ||
            e->type == EVENT_MOUSE_UP) {
            mx = e->x;
            my = e->y;
            consumed = handle_mouse_for_ui(e);
        }
        if (!consumed)
            window_event_handle(*e);
        if (g_drag.active && e->type == EVENT_MOUSE_MOVE) {

            Window *src_win = 0;
            for (int _i = 0; _i < window_count; _i++) {
                if (window_stack[_i]->owner_pid == g_drag.source_pid) {
                    src_win = window_stack[_i];
                    break;
                }
            }
            if (src_win) {
                message_t m_ghost = *msg;
                Event *eg = (Event *)m_ghost.payload;
                eg->x = mx - src_win->x;
                eg->y = my - src_win->y;
                m_ghost.data2 = eg->x;
                m_ghost.data3 = eg->y;
                qk_sys_ipc_send(g_drag.source_pid, &m_ghost);
            }
        }
        if (g_drag.active && (e->type == EVENT_MOUSE_MOVE || e->type == EVENT_MOUSE_UP)) {
            Window *hit = window_at_point(mx, my);
            int new_target_pid = (hit && hit->owner_pid != g_drag.source_pid)
                                     ? hit->owner_pid
                                     : -1;
            if (new_target_pid != g_drag.target_pid) {
                if (g_drag.target_pid >= 0) {
                    message_t left;
                    memset(&left, 0, sizeof(left));
                    left.type = MSG_DRAG_LEFT;
                    qk_sys_ipc_send(g_drag.target_pid, &left);
                }
                g_drag.target_pid = new_target_pid;
                if (g_drag.target_pid >= 0) {
                    message_t over;
                    memset(&over, 0, sizeof(over));
                    over.type = MSG_DRAG_OVER;
                    over.data1 = g_drag.data_type;
                    over.data2 = (uint32_t)(hit ? mx - hit->x : mx);
                    over.data3 = (uint32_t)(hit ? my - hit->y : my);
                    over.sender_pid = g_drag.source_pid;
                    memcpy(over.payload, g_drag.data, g_drag.data_len);
                    qk_sys_ipc_send(g_drag.target_pid, &over);
                }
            } else if (e->type == EVENT_MOUSE_MOVE && g_drag.target_pid >= 0) {
                message_t over;
                memset(&over, 0, sizeof(over));
                over.type = MSG_DRAG_OVER;
                over.data1 = g_drag.data_type;
                over.data2 = (uint32_t)(hit ? mx - hit->x : mx);
                over.data3 = (uint32_t)(hit ? my - hit->y : my);
                over.sender_pid = g_drag.source_pid;
                memcpy(over.payload, g_drag.data, g_drag.data_len);
                qk_sys_ipc_send(g_drag.target_pid, &over);
            }
            if (e->type == EVENT_MOUSE_UP) {
                if (g_drag.target_pid >= 0) {
                    message_t drop;
                    memset(&drop, 0, sizeof(drop));
                    drop.type = MSG_DROP;
                    drop.data1 = g_drag.data_type;
                    drop.data2 = (uint32_t)(hit ? mx - hit->x : mx);
                    drop.data3 = (uint32_t)g_drag.source_pid;
                    drop.sender_pid = g_drag.source_pid;
                    memcpy(drop.payload, g_drag.data, g_drag.data_len);
                    qk_sys_ipc_send(g_drag.target_pid, &drop);

                    message_t cancel;
                    memset(&cancel, 0, sizeof(cancel));
                    cancel.type = MSG_DRAG_CANCEL;
                    cancel.data1 = 0;
                    qk_sys_ipc_send(g_drag.source_pid, &cancel);
                } else {
                    message_t cancel;
                    memset(&cancel, 0, sizeof(cancel));
                    cancel.type = MSG_DRAG_CANCEL;
                    qk_sys_ipc_send(g_drag.source_pid, &cancel);
                }
                memset(&g_drag, 0, sizeof(g_drag));
            }
        }
        if (window_count > 0) {
            Window *top = window_stack[window_count - 1];
            if (e->type == EVENT_KEY_PRESS || e->type == EVENT_KEY_RELEASE) {
                qk_sys_ipc_send(top->owner_pid, msg);
            } else if (!consumed) {
                if (e->type == EVENT_MOUSE_DOWN || e->type == EVENT_MOUSE_UP ||
                    e->type == EVENT_MOUSE_MOVE) {
                    message_t m_mouse = *msg;
                    Event *em = (Event *)m_mouse.payload;
                    em->x -= top->x;
                    em->y -= top->y;
                    m_mouse.data2 = em->x;
                    m_mouse.data3 = em->y;
                    qk_sys_ipc_send(top->owner_pid, &m_mouse);
                } else if (e->type == EVENT_MOUSE_SCROLL) {
                    qk_sys_ipc_send(top->owner_pid, msg);
                }
            }
        }
    } else if (msg->type == MSG_WIN_CREATE) {
        Window *new_win =
            window_create(msg->sender_pid, 100, 100, msg->data1, msg->data2, (char *)msg->payload);
        if (new_win) {
            message_t reply = {.type = MSG_WIN_CREATED, .data1 = new_win->id};
            qk_sys_ipc_send(msg->sender_pid, &reply);
        }
    } else if (msg->type == MSG_WIN_PRINT) {
        for (int i = 0; i < window_count; i++)
            if (window_stack[i]->owner_pid == msg->sender_pid)
                window_print(window_stack[i], (char *)msg->payload);
    } else if (msg->type == MSG_WIN_DRAW_RECT) {
        for (int i = 0; i < window_count; i++) {
            if (window_stack[i]->owner_pid == msg->sender_pid) {
                Window *win = window_stack[i];
                int ry = msg->data1;
                int rx = msg->data2;
                int rw = msg->data3;
                int rh = *(int *)&msg->payload[0];
                uint32_t clr = *(uint32_t *)&msg->payload[4];
                for (int iy = 0; iy < rh; iy++) {
                    for (int ix = 0; ix < rw; ix++) {
                        int tx = rx + ix;
                        int ty = ry + iy;
                        if (tx >= 0 && tx < win->w && ty >= 0 && ty < (win->h - TITLE_BAR_HEIGHT))
                            win->gfx_buffer[ty * win->w + tx] = clr;
                    }
                }
            }
        }
    } else if (msg->type == MSG_WIN_PRINT_AT) {
        for (int i = 0; i < window_count; i++) {
            if (window_stack[i]->owner_pid == msg->sender_pid) {
                Window *win = window_stack[i];
                gfx_context_t win_ctx = {win->gfx_buffer, (uint32_t)win->w,
                                         (uint32_t)(win->h - TITLE_BAR_HEIGHT),
                                         (uint32_t)win->w * 4};
                draw_string_vector(&win_ctx, msg->data1, msg->data2, (char *)msg->payload,
                                   msg->data3);
            }
        }
    } else if (msg->type == MSG_WIN_CLEAR) {
        for (int i = 0; i < window_count; i++)
            if (window_stack[i]->owner_pid == msg->sender_pid)
                window_clear(window_stack[i]);
    } else if (msg->type == MSG_WIN_COLOR) {
        for (int i = 0; i < window_count; i++)
            if (window_stack[i]->owner_pid == msg->sender_pid)
                window_set_color(window_stack[i], msg->data1);
    } else if (msg->type == MSG_WIN_BLIT_PIXELS) {
        for (int i = 0; i < window_count; i++) {
            if (window_stack[i]->owner_pid == msg->sender_pid) {
                Window *win = window_stack[i];
                int px = (int)msg->data1;
                int py = (int)msg->data2;
                int count = (int)msg->data3;
                int content_h = win->h - TITLE_BAR_HEIGHT;
                uint32_t *pixels = (uint32_t *)msg->payload;
                if (count > 128)
                    count = 128;
                for (int p = 0; p < count; p++) {
                    int x = px + p;
                    if (x >= 0 && x < win->w && py >= 0 && py < content_h)
                        win->gfx_buffer[py * win->w + x] = pixels[p];
                }
                break;
            }
        }
    } else if (msg->type == MSG_WIN_MAP_BUFFER) {
        int win_id = (int)msg->data1;
        Window *win = 0;
        for (int i = 0; i < window_count; i++) {
            if (window_stack[i]->id == win_id && window_stack[i]->owner_pid == msg->sender_pid) {
                win = window_stack[i];
                break;
            }
        }
        message_t reply;
        memset(&reply, 0, sizeof(reply));
        if (win && win->gfx_buffer) {
            int content_h = win->h - TITLE_BAR_HEIGHT;
            if (content_h < 1)
                content_h = 1;
            uint32_t buf_size = (uint32_t)win->w * (uint32_t)content_h * 4;
            uint32_t buf_virt = (uint32_t)(uintptr_t)win->gfx_buffer;
            if (qk_grant_foreign_map(msg->sender_pid, buf_virt, buf_size) == 0) {
                reply.type = MSG_WIN_BUFFER_INFO;
                reply.data1 = buf_size;
                reply.data2 = (uint32_t)win->w;
                reply.data3 = (uint32_t)content_h;
                ((uint32_t *)reply.payload)[0] = buf_virt;
            } else {
                reply.type = MSG_ERROR;
            }
        } else {
            reply.type = MSG_ERROR;
        }
        qk_sys_ipc_send(msg->sender_pid, &reply);
    } else if (msg->type == MSG_WIN_DIRTY) {
        (void)msg;
    } else if (msg->type == MSG_CLIP_SET) {
        int len = (int)msg->data1;
        if (len > CLIP_MAX)
            len = CLIP_MAX;
        if (len < 0)
            len = 0;
        memcpy(g_clipboard, msg->payload, len);
        g_clipboard[len] = 0;
        g_clip_len = len;
    } else if (msg->type == MSG_CLIP_GET) {
        message_t reply;
        memset(&reply, 0, sizeof(reply));
        reply.type = MSG_CLIP_DATA;
        reply.data1 = (uint32_t)g_clip_len;
        if (g_clip_len > 0)
            memcpy(reply.payload, g_clipboard, g_clip_len);
        qk_sys_ipc_send(msg->sender_pid, &reply);
    } else if (msg->type == MSG_DRAG_START) {
        if (!g_drag.active) {
            g_drag.active = 1;
            g_drag.source_pid = msg->sender_pid;
            g_drag.target_pid = -1;
            g_drag.data_type = msg->data1;
            g_drag.data_len = msg->data2 < 4096 ? msg->data2 : 4096;
            memcpy(g_drag.data, msg->payload, g_drag.data_len);
        }
    } else if (msg->type == MSG_PROCESS_TERMINATED) {
        int target_pid = msg->data1;
        for (int i = 0; i < window_count; i++) {
            if (window_stack[i]->owner_pid == target_pid) {
                close_window(window_stack[i]);
                i--;
            }
        }
    } else if (msg->type == MSG_INPUT_INJECT) {
        if (msg->sender_pid != qk_get_server_pid(QK_SERVER_INPUT))
            return;
        int source_pid = (int)msg->data3;
        int sender_has_window = 0;
        for (int i = 0; i < window_count; ++i)
            if (window_stack[i] && window_stack[i]->owner_pid == source_pid)
                sender_has_window = 1;
        if (!sender_has_window)
            return;
        for (int i = window_count - 1; i >= 0; --i) {
            Window *target = window_stack[i];
            if (target && target->owner_pid != source_pid) {
                message_t event = *msg;
                event.type = MSG_KEY_EVENT;
                qk_sys_ipc_send(target->owner_pid, &event);
                break;
            }
        }
    }
}

void render() {
    if (wallpaper_rgba && wallpaper_width > 0 && wallpaper_height > 0) {
        uint32_t crop_w = (uint32_t)wallpaper_width;
        uint32_t crop_h = (uint32_t)wallpaper_height;
        uint32_t crop_x = 0, crop_y = 0;
        if (crop_w * fb.height > crop_h * fb.width) {
            uint32_t new_w = crop_h * fb.width / fb.height;
            crop_x = (crop_w - new_w) / 2;
            crop_w = new_w;
        } else {
            uint32_t new_h = crop_w * fb.height / fb.width;
            crop_y = (crop_h - new_h) / 2;
            crop_h = new_h;
        }
        for (uint32_t y = 0; y < fb.height; y++) {
            uint32_t sy = crop_y + y * crop_h / fb.height;
            for (uint32_t x = 0; x < fb.width; x++) {
                uint32_t sx = crop_x + x * crop_w / fb.width;
                uint8_t *px = wallpaper_rgba + ((sy * wallpaper_width + sx) * 4);
                ctx.buffer[y * fb.width + x] = 0xFF000000u | ((uint32_t)px[0] << 16) |
                                               ((uint32_t)px[1] << 8) | px[2];
            }
        }
    } else {
        memset32(ctx.buffer, UI_SURFACE_COLOR, (fb.width * fb.height));
    }
    draw_windows(&ctx);
    desktop_shell_render_bar(&ctx, current_fps);
    if (power_overlay_visible || sleep_mode_active) {
        int w = (int)ctx.width;
        int h = (int)ctx.height;
        if (!blur_snapshot_valid) {
            ensure_blur_snapshot(w, h);
            memcpy32(blur_snapshot, ctx.buffer, w * h * 4);
            if (sleep_mode_active) {
                blur_buffer_inplace(blur_snapshot, w, h, 14);
                gfx_context_t tmp_ctx = {blur_snapshot, (uint32_t)w, (uint32_t)h, (uint32_t)w * 4};
                desktop_shell_dim(&tmp_ctx, UI_SURFACE_COLOR, 180);
            }
            blur_snapshot_valid = 1;
        }
        if (sleep_mode_active)
            memcpy32(ctx.buffer, blur_snapshot, w * h * 4);
    }
    render_power_overlay();
    render_sleep_screen();
    render_desktop_notification();
    render_debug_overlay();

    if (g_drag.active && g_drag.data_type == 1 && g_drag.data_len >= 9) {
        const char *drag_fn = (const char *)(g_drag.data + 8);
        int icon_id = desk_icon_id(drag_fn);
        uint32_t ic = (icon_id == ICON_IMAGE) ? 0xFF88AADDu
                      : (icon_id == ICON_BINARY) ? 0xFF80CC80u
                                                 : 0xFF9A9A9Au;
        int gs = 80;
        int gsz = 40;
        int gx = mx - gs / 2;
        int gy = my - gs / 2;
        if (gx < 0) gx = 0;
        if (gy < 0) gy = 0;
        if (gx + gs > (int)ctx.width) gx = (int)ctx.width - gs;
        if (gy + gs > (int)ctx.height) gy = (int)ctx.height - gs;
        gfx_rect_rounded_fill(&ctx, gx, gy, gs, gs, 8, 0xCC2E2E2Eu);
        if (gui_icons)
            draw_icon_sized(&ctx, gui_icons, gx + (gs - gsz) / 2, gy + 8, icon_id, ic, gsz);

        char label[16];
        int li = 0;
        while (drag_fn[li] && li < 15) { label[li] = drag_fn[li]; li++; }
        label[li] = 0;
        while (li > 0 && get_string_width(label) > gs - 8) { label[--li] = 0; }
        int tw = get_string_width(label);
        draw_string_vector(&ctx, gx + (gs - tw) / 2, gy + gs - 11, label, 0xFF9A9A9Au);
    }
    compositor_output_frame(&ctx, &fb, mx, my);
    qk_gpu_flush();
    fps_counter++;
}
int main() {
    wallpaper_bmp = load_file_from_storage("/SYS/DAT/BG.BMP", &wallpaper_size);
    if (wallpaper_bmp && wallpaper_size <= 0x7FFFFFFFu)
        wallpaper_rgba = stbi_load_from_memory(wallpaper_bmp, (int)wallpaper_size,
                                               &wallpaper_width, &wallpaper_height, 0, 4);
    icon_font_load_from_disk("/SYS/DAT/LUCIDE.TTF");
    if (qk_map_framebuffer(&fb) != 0)
        return 1;
    ctx = (gfx_context_t){(uint32_t *)malloc(fb.width * fb.height * 4), fb.width, fb.height,
                          fb.width * 4};
    if (!ctx.buffer)
        return 1;
    font_atlas_load_from_disk("/SYS/DAT/INTER.TTF");
    window_manager_init();
    window_manager_set_screen_size((int)fb.width, (int)fb.height);
    last_fps_update = qk_get_uptime();
    uint32_t last_render_ms = qk_get_uptime();
    message_t msg;
    while (1) {
        while (qk_ipc_recv_nb(&msg) == 0)
            process_message(&msg);

        uint32_t now = qk_get_uptime();
        current_time_ms = now;
        if (now - last_fps_update >= 1000) {
            current_fps = fps_counter;
            fps_counter = 0;
            last_fps_update = now;
        }

        if ((now - last_render_ms) >= 30) {
            render();
            last_render_ms = now;
        }
        qk_yield();
    }
    return 0;
}
