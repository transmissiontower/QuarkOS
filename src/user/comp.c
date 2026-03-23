#include "user/lib/libapi.h"
#include "user/lib/gfx.h"
#include "user/lib/window.h"
#include "gfx/events.h"
#include "gfx/bitmaps.h"
#include "user/lib/font_atlas.h"
#include "user/lib/icons_def.h"
#include "user/lib/path.h"
#include "lib/stdlib.h"

fb_info_t fb;
gfx_context_t ctx;
static gfx_context_t fb_ctx;
int32_t mx = 512, my = 384;
uint32_t fps_counter = 0, last_fps_update = 0, *wallpaper_data = 0;
int current_fps = 0;

static int debug_overlay_visible = 0;
static uint32_t total_ipc_messages = 0;

typedef struct {
    int x, y, w, h;
} ui_rect_t;

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
#define POWER_MENU_WIDTH   125
#define POWER_MENU_ITEM_H  40
#define POWER_MENU_RADIUS  9
#define POWER_ICON_SCALE   1
#define POWER_MENU_UP_OFFSET 8

static ui_rect_t power_button_rect = {0, 0, 0, 0};
static ui_rect_t terminal_button_rect = {0, 0, 0, 0};
static ui_rect_t brightness_button_rect = {0, 0, 0, 0};
static int power_overlay_visible = 0;
static int power_overlay_hover = -1;
static int sleep_mode_active = 0;

static power_action_t power_actions[POWER_ACTION_COUNT] = {
    { "Shutdown", ICON_SHUTDOWN, POWER_ACTION_SHUTDOWN, {0,0,0,0} },
    { "Reboot",   ICON_UP,       POWER_ACTION_REBOOT,   {0,0,0,0} },
    { "Sleep",    ICON_SUN,      POWER_ACTION_SLEEP,    {0,0,0,0} }
};

#define BRIGHTNESS_LEVEL_COUNT 4
static const uint8_t brightness_alpha_levels[BRIGHTNESS_LEVEL_COUNT] = {0, 40, 90, 150};
static const int brightness_percent_levels[BRIGHTNESS_LEVEL_COUNT] = {100, 85, 65, 40};
static int brightness_level_index = 0;

static uint32_t *blur_buffer = 0;
static int blur_buf_w = 0;
static int blur_buf_h = 0;
static uint32_t *blur_snapshot = 0;
static int blur_snapshot_w = 0;
static int blur_snapshot_h = 0;
static int blur_snapshot_valid = 0;

typedef struct {
    int x0, y0, x1, y1;
} rect_i_t;
static rect_i_t prev_cursor_rect = {0, 0, 0, 0};
static int prev_cursor_valid = 0;
static int storage_server_pid = -1;
static uint32_t current_time_ms = 0;

static uint32_t desktop_notification_until = 0;
static char desktop_notification_text[64];
static uint8_t *terminal_binary = 0;
static uint32_t terminal_binary_size = 0;

#define DESKTOP_NOTIFICATION_MS 3200

static void process_message(message_t *msg);

static inline int clamp_int(int v, int min, int max) {
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

static inline int abs_int(int v) {
    return (v < 0) ? -v : v;
}

static uint8_t bilinear_sample_alpha(const GlyphInfo *g, int u_fp, int v_fp) {
    int x = u_fp >> 16;
    int y = v_fp >> 16;
    int wu = (u_fp >> 8) & 0xFF; 
    int wv = (v_fp >> 8) & 0xFF; 
    const uint32_t *bmp = gui_icons->bitmap;
    int stride = gui_icons->tex_width;
    uint8_t a00 = bmp[(g->y + y) * stride + (g->x + x)] >> 24;
    uint8_t a10 = (x + 1 < g->w) ? (bmp[(g->y + y) * stride + (g->x + x + 1)] >> 24) : a00;
    uint8_t a01 = (y + 1 < g->h) ? (bmp[(g->y + y + 1) * stride + (g->x + x)] >> 24) : a00;
    uint8_t a11 = (x + 1 < g->w && y + 1 < g->h) ? (bmp[(g->y + y + 1) * stride + (g->x + x + 1)] >> 24) : a10;
    int top = a00 + ((a10 - a00) * wu >> 8);
    int bottom = a01 + ((a11 - a01) * wu >> 8);
    return (uint8_t)(top + ((bottom - top) * wv >> 8));
}

static int ensure_storage_pid(void) {
    if (storage_server_pid < 0) storage_server_pid = qk_get_server_pid(QK_SERVER_STORAGE);
    return (storage_server_pid < 0) ? -1 : 0;
}

static void desktop_show_notification(const char *text) {
    if (!text) return;
    strncpy(desktop_notification_text, text, sizeof(desktop_notification_text) - 1);
    desktop_notification_until = qk_get_uptime() + DESKTOP_NOTIFICATION_MS;
}

static int wait_for_storage_reply(message_t *resp) {
    if (!resp) return -1;
    message_t msg;
    while (1) {
        if (qk_ipc_recv(&msg) != 0) continue;
        if (msg.sender_pid == storage_server_pid) {
            memcpy(resp, &msg, sizeof(message_t));
            return 0;
        }
        process_message(&msg);
    }
}

static void *load_file_from_storage(const char *filename, uint32_t *out_size) {
    if (out_size) *out_size = 0;
    if (!filename || ensure_storage_pid() != 0) return 0;
    uint16_t dir_cluster = 0;
    char resolved_name[16];
    const char *target_name = filename;
    if (strstr(filename, "/")) {
        if (qk_path_resolve(filename, &dir_cluster, resolved_name) != 0) return 0;
        target_name = resolved_name;
    }
    message_t req;
    memset(&req, 0, sizeof(message_t));
    req.type = MSG_FILE_OPEN;
    req.data1 = dir_cluster;
    strncpy((char*)req.payload, target_name, 12);
    if (qk_sys_ipc_send(storage_server_pid, &req) != 0) return 0;
    message_t resp;
    if (wait_for_storage_reply(&resp) != 0) return 0;
    if (resp.type != MSG_FILE_SIZE || resp.data1 == 0) return 0;
    uint32_t size = resp.data1;
    if (out_size) *out_size = size;
    uint8_t *buffer = (uint8_t*)malloc(size);
    if (!buffer) return 0;
    uint32_t offset = 0;
    while (offset < size) {
        req.type = MSG_REQ_DATA;
        req.data1 = offset;
        req.data2 = 100;
        if (qk_sys_ipc_send(storage_server_pid, &req) != 0) break;
        if (wait_for_storage_reply(&resp) != 0) break;
        if (resp.type != MSG_FILE_DATA) break;
        uint32_t chunk = resp.data1;
        if (chunk == 0) break;
        if (chunk > sizeof(resp.payload)) chunk = sizeof(resp.payload);
        if (offset + chunk > size) chunk = size - offset;
        memcpy(buffer + offset, resp.payload, chunk);
        offset += chunk;
    }
    if (offset != size) {
        free(buffer);
        return 0;
    }
    return buffer;
}

static void cache_terminal_binary(void) {
    if (terminal_binary) return;
    uint32_t size = 0;
    void *buf = load_file_from_storage("/APPS/SHELL.QEX", &size);
    if (!buf || size == 0) {
        if (buf) free(buf);
        return;
    }
    terminal_binary = (uint8_t*)buf;
    terminal_binary_size = size;
}

static void ensure_blur_buffer(int w, int h) {
    if (blur_buffer && blur_buf_w == w && blur_buf_h == h) return;
    if (blur_buffer) free(blur_buffer);
    blur_buffer = (uint32_t*)malloc(sizeof(uint32_t) * w * h);
    blur_buf_w = w;
    blur_buf_h = h;
}

static void ensure_blur_snapshot(int w, int h) {
    if (blur_snapshot && blur_snapshot_w == w && blur_snapshot_h == h) return;
    if (blur_snapshot) free(blur_snapshot);
    blur_snapshot = (uint32_t*)malloc(sizeof(uint32_t) * w * h);
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
            dst[base + x] = 0xFF000000 | ((sum_r / gauss_weight_sum) << 16) | ((sum_g / gauss_weight_sum) << 8) | (sum_b / gauss_weight_sum);
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
            dst[y * w + x] = 0xFF000000 | ((sum_r / gauss_weight_sum) << 16) | ((sum_g / gauss_weight_sum) << 8) | (sum_b / gauss_weight_sum);
        }
    }
}

static void blur_buffer_inplace(uint32_t *buffer, int w, int h, int intensity) {
    if (intensity <= 0) return;
    ensure_blur_buffer(w, h);
    int passes = clamp_int(intensity / 6 + 1, 1, 4);
    for (int i = 0; i < passes; i++) {
        blur_horizontal(buffer, blur_buffer, w, h);
        blur_vertical(blur_buffer, buffer, w, h);
    }
}

static void apply_screen_dim(gfx_context_t *context, uint32_t color, uint8_t alpha) {
    if (!alpha) return;
    uint32_t total = context->width * context->height;
    for (uint32_t i = 0; i < total; i++) {
        context->buffer[i] = gfx_blend_colors(color, context->buffer[i], alpha);
    }
}

static void apply_brightness_overlay(void) {
    uint8_t alpha = brightness_alpha_levels[brightness_level_index];
    if (!alpha) return;
    apply_screen_dim(&ctx, 0xFF000000, alpha);
}

static void cycle_brightness_level(void) {
    brightness_level_index = (brightness_level_index + 1) % BRIGHTNESS_LEVEL_COUNT;
    char msg[32] = "Brightness ";
    char value[8];
    int_to_str(brightness_percent_levels[brightness_level_index], value, 10);
    strcat(msg, value);
    strcat(msg, "%");
    desktop_show_notification(msg);
}

static void draw_icon_scaled_centered(int icon_id, int center_x, int center_y, int scale, uint32_t color) {
    if (!gui_icons || icon_id < 32 || scale <= 0) return;
    GlyphInfo *g = &gui_icons->glyphs[icon_id - 32];
    int icon_w = g->w * scale;
    int icon_h = g->h * scale;
    int start_x = center_x - icon_w / 2;
    int start_y = center_y - icon_h / 2;
    int step_fp = (1 << 16) / scale;
    for (int dy = 0; dy < icon_h; dy++) {
        int v_fp = dy * step_fp; 
        for (int dx = 0; dx < icon_w; dx++) {
            int u_fp = dx * step_fp; 
            uint8_t a = bilinear_sample_alpha(g, u_fp, v_fp);
            if (a > 2) { 
                int sharp_a = (a * 1.2); 
                if (sharp_a > 255) sharp_a = 255;
                gfx_put_pixel_aa(&ctx, start_x + dx, start_y + dy, color, (uint8_t)sharp_a);
            }
        }
    }
}

static int point_in_rect(ui_rect_t rect, int px, int py) {
    return (px >= rect.x && px < rect.x + rect.w && py >= rect.y && py < rect.y + rect.h);
}

static void layout_power_menu(void) {
    if (!ctx.width || !ctx.height) return;
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
        if (point_in_rect(power_actions[i].bounds, x, y)) return i;
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
        case POWER_ACTION_SHUTDOWN: qk_power_shutdown(); break;
        case POWER_ACTION_REBOOT:   qk_power_reboot(); break;
        case POWER_ACTION_SLEEP:    enter_sleep_mode(); break;
    }
}

static void launch_terminal_process(void) {
    if (!terminal_binary || terminal_binary_size == 0) {
        desktop_show_notification("Terminal unavailable");
        return;
    }
    if (qk_spawn(terminal_binary, terminal_binary_size) != 0) desktop_show_notification("Failed to launch terminal");
}

static int handle_mouse_for_ui(Event *e) {
    if (sleep_mode_active) {
        if (e->type == EVENT_MOUSE_DOWN || e->type == EVENT_MOUSE_MOVE || e->type == EVENT_MOUSE_UP) exit_sleep_mode();
        return 1;
    }
    if (power_overlay_visible) {
        if (e->type == EVENT_MOUSE_MOVE) {
            power_overlay_hover = find_power_action_at(e->x, e->y);
            return 1;
        }
        if (e->type == EVENT_MOUSE_DOWN && e->button == MOUSE_BTN_LEFT) {
            int idx = find_power_action_at(e->x, e->y);
            if (idx >= 0) trigger_power_action(power_actions[idx].type);
            else close_power_overlay();
            return 1;
        }
        if (e->type == EVENT_MOUSE_UP) return 1;
    }
    if (e->type == EVENT_MOUSE_DOWN && e->button == MOUSE_BTN_LEFT) {
        if (point_in_rect(terminal_button_rect, e->x, e->y)) {
            launch_terminal_process();
            return 1;
        }
        if (point_in_rect(brightness_button_rect, e->x, e->y)) {
            cycle_brightness_level();
            return 1;
        }
        if (point_in_rect(power_button_rect, e->x, e->y)) {
            if (power_overlay_visible) close_power_overlay();
            else open_power_overlay();
            return 1;
        }
    }
    return 0;
}

void draw_pill(int x, int y, int w, int h) {
    gfx_rect_rounded_fill(&ctx, x, y, w, h, 7, UI_SURFACE_COLOR);
}

void render_top_bar() {
    int y = 10;
    int h = 35;
    uint32_t fg = COLOR_WIN_ACTIVE;
    uint32_t brightness_icon_color = (brightness_level_index == 0) ? fg : gfx_blend_colors(0xFFFFFFFF, fg, 160);
    int search_x = 10;
    int search_w = 35;
    draw_pill(search_x, y, search_w, h);
    draw_icon(&ctx, gui_icons, search_x + (search_w - 16)/2 - 1, y + (h - 16)/2 - 1, ICON_SEARCH, fg);
    int clock_x = 1920/2 - 75;
    int clock_w = 150;
    int clock_y = y + 9;
    draw_pill(clock_x, y, clock_w, h);
    qk_time_t t;
    qk_get_time(&t);
    char time_str[40] = {0};
    char tmp[12];
    time_to_str(t.hour, tmp); strcat(time_str, tmp); strcat(time_str, ":");
    time_to_str(t.minute, tmp); strcat(time_str, tmp);
    draw_string_vector(&ctx, clock_x + 20, clock_y, "Mar 14", fg);
    draw_string_vector(&ctx, clock_x + 100, clock_y, time_str, fg);
    int buttons_x = 1920 - 135;
    int buttons_w = 125;
    int buttons_y = 19;
    draw_pill(buttons_x, y, buttons_w, h);
    draw_icon(&ctx, gui_icons, buttons_x + 10, buttons_y, ICON_TERMINAL, fg);
    draw_icon(&ctx, gui_icons, buttons_x + 36, buttons_y, ICON_SUN, brightness_icon_color);
    draw_icon(&ctx, gui_icons, buttons_x + 68, buttons_y, ICON_KEYBOARD, fg);
    draw_icon(&ctx, gui_icons, buttons_x + 98, buttons_y, ICON_SHUTDOWN, fg);
    terminal_button_rect.x = buttons_x + 10 - 4; terminal_button_rect.y = buttons_y - 4;
    terminal_button_rect.w = 28; terminal_button_rect.h = 28;
    brightness_button_rect.x = buttons_x + 36 - 4; brightness_button_rect.y = buttons_y - 4;
    brightness_button_rect.w = 28; brightness_button_rect.h = 28;
    power_button_rect.x = buttons_x + 98 - 4; power_button_rect.y = buttons_y - 4;
    power_button_rect.w = 28; power_button_rect.h = 28;
    int fps_x = buttons_x - 110;
    int fps_w = 100;
    int fps_y = y + 9;
    draw_pill(fps_x, y, fps_w, h);
    char fps_buf[16];
    int_to_str(current_fps, fps_buf, 10);
    draw_string_vector(&ctx, fps_x + 20, fps_y, "FPS", fg);
    draw_string_vector(&ctx, fps_x + 60, fps_y, fps_buf, fg);
}

void render_power_overlay(void) {
    if (!power_overlay_visible) return;
    layout_power_menu();
    int menu_h = POWER_ACTION_COUNT * POWER_MENU_ITEM_H + 10;
    int x = power_button_rect.x + power_button_rect.w - POWER_MENU_WIDTH;
    int y = power_button_rect.y + power_button_rect.h + POWER_MENU_UP_OFFSET;
    gfx_draw_shadow(&ctx, x, y, POWER_MENU_WIDTH, menu_h, POWER_MENU_RADIUS, 12);
    gfx_rect_rounded_fill(&ctx, x, y, POWER_MENU_WIDTH, menu_h, POWER_MENU_RADIUS, UI_SURFACE_COLOR);
    for (int i = 0; i < POWER_ACTION_COUNT; i++) {
        ui_rect_t r = power_actions[i].bounds;
        if (i == power_overlay_hover) gfx_rect_rounded_fill(&ctx, r.x, r.y, r.w, r.h, 8, 0x33000000);
        draw_icon_scaled_centered(power_actions[i].icon_id, r.x + 16, r.y + r.h / 2, POWER_ICON_SCALE, COLOR_WIN_ACTIVE);
        draw_string_vector(&ctx, r.x + 36, r.y + r.h / 2 - 8, power_actions[i].label, COLOR_WIN_ACTIVE);
    }
}

void render_sleep_screen(void) {
    if (!sleep_mode_active) return;
    gfx_rect_fill(&ctx, 0, 0, (int)ctx.width, (int)ctx.height, 0xCC000000);
    const char *title = "Sleeping...";
    const char *hint = "Press any key or click to wake";
    int cx = (int)ctx.width / 2;
    draw_string_vector(&ctx, cx - (int)strlen(title) * 6, (int)ctx.height / 2 - 10, title, COLOR_WIN_ACTIVE);
    draw_string_vector(&ctx, cx - (int)strlen(hint) * 4, (int)ctx.height / 2 + 20, hint, COLOR_WIN_ACTIVE);
}

static void render_desktop_notification(void) {
    if (!desktop_notification_until || current_time_ms >= desktop_notification_until || !desktop_notification_text[0]) return;
    int padding = 20;
    int h = 42;
    int text_w = (int)strlen(desktop_notification_text) * 8;
    int w = text_w + padding * 2;
    if (w < 160) w = 160;
    int x = 60;
    int y = (int)ctx.height - h - 60;
    gfx_rect_rounded_fill(&ctx, x, y, w, h, 16, 0xAA000000);
    draw_string_vector(&ctx, x + padding, y + 12, desktop_notification_text, COLOR_WIN_ACTIVE);
}

void render_debug_overlay(void) {
    if (!debug_overlay_visible) return;
    int x = 20; int y = 60;
    uint32_t used_mem, total_mem, cpu_load;
    qk_get_stats(&used_mem, &total_mem, &cpu_load);
    uint32_t uptime = qk_get_uptime();
    process_info_t procs[32];
    int task_count = qk_get_procs(procs, 32);

    char buf[64]; char val[16];

    draw_string_vector(&ctx, x, y, "--- SYSTEM DEBUG ---", 0xFFFFFFFF);

    
    memset(buf, 0, 64);
    strcat(buf, "CPU Load: "); 
    int_to_str(cpu_load, val, 10); 
    strcat(buf, val); strcat(buf, "%");
    draw_string_vector(&ctx, x, y + 25, buf, 0xFF55FF55);

    
    memset(buf, 0, 64);
    strcat(buf, "Memory: "); 
    int_to_str(used_mem / 1024 / 1024, val, 10); 
    strcat(buf, val); strcat(buf, " MB used");
    draw_string_vector(&ctx, x, y + 45, buf, 0xFF55FFFF);

    
    memset(buf, 0, 64);
    strcat(buf, "Uptime: "); 
    int_to_str(uptime / 1000, val, 10); 
    strcat(buf, val); strcat(buf, "s");
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
        Event* e = (Event*)msg->payload;
        if (e->type == EVENT_KEY_PRESS && msg->data1 == 0x58) { debug_overlay_visible = !debug_overlay_visible; return; }
        if (e->type == EVENT_KEY_PRESS || e->type == EVENT_KEY_RELEASE) {
            if (sleep_mode_active) { if (e->type == EVENT_KEY_PRESS) exit_sleep_mode(); return; }
            if (power_overlay_visible) { if (e->type == EVENT_KEY_PRESS && e->key == 0x01) close_power_overlay(); return; }
        }
        int consumed = 0;
        if (e->type == EVENT_MOUSE_MOVE || e->type == EVENT_MOUSE_DOWN || e->type == EVENT_MOUSE_UP) {
            mx = e->x; my = e->y; consumed = handle_mouse_for_ui(e);
        }
        if (!consumed) window_event_handle(*e);
        if (window_count > 0) {
            Window* top = window_stack[window_count - 1];
            if (e->type == EVENT_KEY_PRESS || e->type == EVENT_KEY_RELEASE) {
                qk_sys_ipc_send(top->owner_pid, msg);
            } else if (e->type == EVENT_MOUSE_DOWN || e->type == EVENT_MOUSE_UP || e->type == EVENT_MOUSE_MOVE) {
                message_t m_mouse = *msg;
                Event* em = (Event*)m_mouse.payload;
                em->x -= top->x;
                em->y -= top->y;
                m_mouse.data2 = em->x;
                m_mouse.data3 = em->y;
                qk_sys_ipc_send(top->owner_pid, &m_mouse);
            }
        }
    } else if (msg->type == MSG_WIN_CREATE) {
        Window* new_win = window_create(msg->sender_pid, 100, 100, msg->data1, msg->data2, (char*)msg->payload);
        if (new_win) { message_t reply = {.type = MSG_WIN_CREATED, .data1 = new_win->id}; qk_sys_ipc_send(msg->sender_pid, &reply); }
    } else if (msg->type == MSG_WIN_PRINT) {
        for(int i=0; i<window_count; i++) if(window_stack[i]->owner_pid == msg->sender_pid) window_print(window_stack[i], (char*)msg->payload);
    } else if (msg->type == MSG_WIN_DRAW_RECT) {
        for(int i = 0; i < window_count; i++) {
            if(window_stack[i]->owner_pid == msg->sender_pid) {
                Window* win = window_stack[i];
                int ry = msg->data1; int rx = msg->data2; int rw = msg->data3;
                int rh = *(int*)&msg->payload[0]; uint32_t clr = *(uint32_t*)&msg->payload[4];
                for(int iy = 0; iy < rh; iy++) {
                    for(int ix = 0; ix < rw; ix++) {
                        int tx = rx + ix; int ty = ry + iy;
                        if(tx >= 0 && tx < win->w && ty >= 0 && ty < (win->h - TITLE_BAR_HEIGHT)) win->gfx_buffer[ty * win->w + tx] = clr;
                    }
                }
            }
        }
    } else if (msg->type == MSG_WIN_PRINT_AT) {
        for(int i = 0; i < window_count; i++) {
            if(window_stack[i]->owner_pid == msg->sender_pid) {
                Window* win = window_stack[i];
                gfx_context_t win_ctx = {win->gfx_buffer, (uint32_t)win->w, (uint32_t)(win->h - TITLE_BAR_HEIGHT), (uint32_t)win->w * 4};
                draw_string_vector(&win_ctx, msg->data1, msg->data2, (char*)msg->payload, msg->data3);
            }
        }
    } else if (msg->type == MSG_WIN_CLEAR) {
        for(int i=0; i<window_count; i++) if(window_stack[i]->owner_pid == msg->sender_pid) window_clear(window_stack[i]);
    } else if (msg->type == MSG_WIN_COLOR) {
        for(int i=0; i<window_count; i++) if(window_stack[i]->owner_pid == msg->sender_pid) window_set_color(window_stack[i], msg->data1);
    } else if (msg->type == MSG_PROCESS_TERMINATED) {
        int target_pid = msg->data1;
        for (int i = 0; i < window_count; i++) {
            if (window_stack[i]->owner_pid == target_pid) {
                close_window(window_stack[i]);
                i--;
            }
        }
    }
}

static inline void copy_row_segment(uint8_t *dst_row, uint8_t *src_row, int start_px, int end_px) {
    if (end_px <= start_px) return;
    memcpy(dst_row + start_px * 4, src_row + start_px * 4, (end_px - start_px) * 4);
}

static inline int rect_has_area(rect_i_t r) { return (r.x1 > r.x0) && (r.y1 > r.y0); }
static inline int rect_equals(rect_i_t a, rect_i_t b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }

static inline rect_i_t clamp_cursor_rect(int x, int y, int max_w, int max_h) {
    rect_i_t r;
    r.x0 = clamp_int(x, 0, max_w); r.y0 = clamp_int(y, 0, max_h);
    r.x1 = clamp_int(x + CURSOR_W, 0, max_w); r.y1 = clamp_int(y + CURSOR_H, 0, max_h);
    return r;
}

static void copy_rect_from_ctx(rect_i_t r, uint8_t *dst, uint8_t *src, int dst_stride, int src_stride) {
    if (!rect_has_area(r)) return;
    int w_bytes = (r.x1 - r.x0) * 4;
    for (int y = r.y0; y < r.y1; ++y) memcpy(dst + y * dst_stride + r.x0 * 4, src + y * src_stride + r.x0 * 4, w_bytes);
}

typedef struct { int x0, x1; } interval_t;

static inline void add_interval_for_row(rect_i_t r, int y, interval_t *out, int *count, int max_w) {
    if (y < r.y0 || y >= r.y1) return;
    int x0 = clamp_int(r.x0, 0, max_w); int x1 = clamp_int(r.x1, 0, max_w);
    if (x1 <= x0) return;
    for (int i = 0; i < *count; ++i) if (out[i].x0 == x0 && out[i].x1 == x1) return;
    out[*count].x0 = x0; out[*count].x1 = x1; (*count)++;
}

static void present_frame(void) {
    if (!fb.buffer || !ctx.buffer) return;
    int screen_w = (int)ctx.width; int screen_h = (int)ctx.height;
    if (screen_w <= 0 || screen_h <= 0) return;
    rect_i_t new_cursor_rect = clamp_cursor_rect(mx, my, screen_w, screen_h);
    int new_cursor_valid = rect_has_area(new_cursor_rect);
    uint8_t *src = (uint8_t*)ctx.buffer; uint8_t *dst = (uint8_t*)fb.buffer;
    int src_stride = screen_w * 4; int dst_stride = (int)fb.pitch; int row_bytes = screen_w * 4;
    if (!prev_cursor_valid && !new_cursor_valid) {
        for (int y = 0; y < screen_h; ++y) memcpy(dst + y * dst_stride, src + y * src_stride, row_bytes);
        if (new_cursor_valid) {
            copy_rect_from_ctx(new_cursor_rect, dst, src, dst_stride, src_stride);
            gfx_draw_sprite(&fb_ctx, mx, my, CURSOR_W, CURSOR_H, cursor, cursor_palette);
            prev_cursor_rect = new_cursor_rect; prev_cursor_valid = 1;
        } else prev_cursor_valid = 0;
        return;
    }
    for (int y = 0; y < screen_h; ++y) {
        uint8_t *dst_row = dst + y * dst_stride; uint8_t *src_row = src + y * src_stride;
        interval_t intervals[2]; int interval_count = 0;
        if (prev_cursor_valid) add_interval_for_row(prev_cursor_rect, y, intervals, &interval_count, screen_w);
        if (!prev_cursor_valid || !rect_equals(prev_cursor_rect, new_cursor_rect)) if (new_cursor_valid) add_interval_for_row(new_cursor_rect, y, intervals, &interval_count, screen_w);
        if (interval_count == 0) { memcpy(dst_row, src_row, row_bytes); continue; }
        if (interval_count == 2 && intervals[0].x0 > intervals[1].x0) { interval_t tmp = intervals[0]; intervals[0] = intervals[1]; intervals[1] = tmp; }
        int cur = 0;
        for (int i = 0; i < interval_count; ++i) {
            if (cur < intervals[i].x0) copy_row_segment(dst_row, src_row, cur, intervals[i].x0);
            if (cur < intervals[i].x1) cur = intervals[i].x1;
        }
        if (cur < screen_w) copy_row_segment(dst_row, src_row, cur, screen_w);
    }
    if (prev_cursor_valid) copy_rect_from_ctx(prev_cursor_rect, dst, src, dst_stride, src_stride);
    if (!prev_cursor_valid || !rect_equals(prev_cursor_rect, new_cursor_rect)) copy_rect_from_ctx(new_cursor_rect, dst, src, dst_stride, src_stride);
    gfx_draw_sprite(&fb_ctx, mx, my, CURSOR_W, CURSOR_H, cursor, cursor_palette);
    if (new_cursor_valid) { prev_cursor_rect = new_cursor_rect; prev_cursor_valid = 1; }
    else prev_cursor_valid = 0;
}

void render() {
    if (wallpaper_data) memcpy32(ctx.buffer, wallpaper_data, (fb.width * fb.height * 4));
    else memset32(ctx.buffer, UI_SURFACE_COLOR, (fb.width * fb.height));
    draw_windows(&ctx);
    render_top_bar();
    if (power_overlay_visible || sleep_mode_active) {
        int w = (int)ctx.width; int h = (int)ctx.height;
        if (!blur_snapshot_valid) {
            ensure_blur_snapshot(w, h);
            memcpy32(blur_snapshot, ctx.buffer, w * h * 4);
            if (sleep_mode_active) {
                blur_buffer_inplace(blur_snapshot, w, h, 14);
                gfx_context_t tmp_ctx = {blur_snapshot, (uint32_t)w, (uint32_t)h, (uint32_t)w * 4};
                apply_screen_dim(&tmp_ctx, UI_SURFACE_COLOR, 180);
            }
            blur_snapshot_valid = 1;
        }
        if (sleep_mode_active) memcpy32(ctx.buffer, blur_snapshot, w * h * 4);
    }
    render_power_overlay();
    render_sleep_screen();
    if (!sleep_mode_active) apply_brightness_overlay();
    render_desktop_notification();
    render_debug_overlay();
    present_frame();
    fps_counter++;
}

int main() {
    void *icons_addr = 0, *font_addr = 0;
    qk_map_wallpaper((void**)&wallpaper_data);
    qk_map_icons_atlas(&icons_addr);
    if (icons_addr) { gui_icons = (AtlasFont*)malloc(sizeof(AtlasFont)); font_atlas_init_for_ptr(gui_icons, (uint8_t*)icons_addr); }
    if (qk_map_framebuffer(&fb) != 0) return 1;
    fb_ctx = (gfx_context_t){fb.buffer, fb.width, fb.height, fb.pitch};
    ctx = (gfx_context_t){(uint32_t*)malloc(fb.width * fb.height * 4), fb.width, fb.height, fb.width * 4};
    if (qk_map_font_atlas(&font_addr) == 0) font_atlas_init((uint8_t*)font_addr);
    window_manager_init();
    cache_terminal_binary();
    last_fps_update = qk_get_uptime();
    message_t msg;
    while(1) {
        while (qk_ipc_recv_nb(&msg) == 0) process_message(&msg);
        uint32_t now = qk_get_uptime();
        current_time_ms = now;
        if (now - last_fps_update >= 1000) { current_fps = fps_counter; fps_counter = 0; last_fps_update = now; }
        render();
        qk_sleep(1);
    }
    return 0;
}
