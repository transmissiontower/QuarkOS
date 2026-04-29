// SPDX-License-Identifier: MIT
/*
 * src/user/comp.c - Window compositor and desktop shell
 */
#include "gfx/bitmaps.h"
#include "gfx/events.h"
#include "lib/stdlib.h"
#include "user/lib/font_atlas.h"
#include "user/lib/gfx.h"
#include "user/lib/icons_def.h"
#include "user/lib/libapi.h"
#include "user/lib/path.h"
#include "user/lib/window.h"
fb_info_t fb;
gfx_context_t ctx;
static gfx_context_t fb_ctx;
int32_t mx = 512, my = 384;
uint32_t fps_counter = 0, last_fps_update = 0, *wallpaper_data = 0;
int current_fps = 0;
static int debug_overlay_visible = 0;
static uint32_t total_ipc_messages = 0;

#define CLIP_MAX 4095
static char  g_clipboard[CLIP_MAX + 1];
static int   g_clip_len = 0;
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
#define POWER_MENU_WIDTH 125
#define POWER_MENU_ITEM_H 40
#define POWER_MENU_RADIUS 9
#define POWER_ICON_SCALE 1
#define POWER_MENU_UP_OFFSET 8
static ui_rect_t power_button_rect = {0, 0, 0, 0};
static ui_rect_t terminal_button_rect = {0, 0, 0, 0};
static ui_rect_t brightness_button_rect = {0, 0, 0, 0};
static ui_rect_t keyboard_button_rect = {0, 0, 0, 0};
static int power_overlay_visible = 0;
static int power_overlay_hover = -1;
static int sleep_mode_active = 0;
static power_action_t power_actions[POWER_ACTION_COUNT] = {
    {"Shutdown", ICON_SHUTDOWN, POWER_ACTION_SHUTDOWN, {0, 0, 0, 0}},
    {"Reboot", ICON_UP, POWER_ACTION_REBOOT, {0, 0, 0, 0}},
    {"Sleep", ICON_SUN, POWER_ACTION_SLEEP, {0, 0, 0, 0}}};
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
static uint8_t *vkbd_binary = 0;
static uint32_t vkbd_binary_size = 0;
static int vkbd_pid = -1;
static int vkbd_is_visible = 0;
static uint32_t *vkbd_mapped_buf = 0;
static int vkbd_buf_w = 0, vkbd_buf_h = 0;
static int vkbd_pos_x = 0, vkbd_pos_y = 0;
#define DESKTOP_NOTIFICATION_MS 3200

#ifndef ICON_IMAGE
#define ICON_IMAGE 0xEA06
#endif
#define DESK_MAX_ICONS 32
#define DESK_ICON_W    90
#define DESK_ICON_H    90
#define DESK_ICON_PAD  12
#define DESK_START_X   24
#define DESK_START_Y   60
typedef struct {
    char name[64];
    int  is_dir;
    uint32_t cluster;
} desk_entry_t;
static desk_entry_t desk_entries[DESK_MAX_ICONS];
static int desk_entry_count = 0;
static int desk_loaded = 0;
static int desk_selected = -1;

static void process_message(message_t *msg);
static inline int clamp_int(int v, int min, int max)
{
    if (v < min)
        return min;
    if (v > max)
        return max;
    return v;
}
static inline int abs_int(int v)
{
    return (v < 0) ? -v : v;
}
static int ensure_storage_pid(void)
{
    if (storage_server_pid < 0)
        storage_server_pid = qk_get_server_pid(QK_SERVER_STORAGE);
    return (storage_server_pid < 0) ? -1 : 0;
}
static void desktop_show_notification(const char *text)
{
    if (!text)
        return;
    strncpy(desktop_notification_text, text, sizeof(desktop_notification_text) - 1);
    desktop_notification_until = qk_get_uptime() + DESKTOP_NOTIFICATION_MS;
}
static int wait_for_storage_reply(message_t *resp)
{
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
static void *load_file_from_storage(const char *filename, uint32_t *out_size)
{
    if (out_size)
        *out_size = 0;
    if (!filename || ensure_storage_pid() != 0)
        return 0;
    uint32_t dir_cluster = 0;
    char resolved_name[16];
    const char *target_name = filename;
    if (strstr(filename, "/")) {
        if (qk_path_resolve(filename, &dir_cluster, resolved_name) != 0)
            return 0;
        target_name = resolved_name;
    }
    message_t req;
    memset(&req, 0, sizeof(message_t));
    req.type = MSG_FILE_OPEN;
    req.data1 = dir_cluster;
    strncpy((char *)req.payload, target_name, 12);
    if (qk_sys_ipc_send(storage_server_pid, &req) != 0)
        return 0;
    message_t resp;
    if (wait_for_storage_reply(&resp) != 0)
        return 0;
    if (resp.type != MSG_FILE_SIZE || resp.data1 == 0)
        return 0;
    uint32_t size = resp.data1;
    if (out_size)
        *out_size = size;
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

static void desk_load(void)
{
    if (desk_loaded) return;
    desk_loaded = 1;
    desk_entry_count = 0;
    if (ensure_storage_pid() != 0) return;

    uint32_t desk_cluster = 0;
    char dummy[64];
    if (qk_path_resolve_ext(0, "/USR/DEFAULT/DESK", &desk_cluster, dummy) != 0)
        return;

    message_t req, resp;
    memset(&req, 0, sizeof(req));
    req.type = MSG_DIR_REQ;
    req.data1 = desk_cluster;
    qk_ipc_send_recv(storage_server_pid, &req, &resp);
    uint16_t desk_cl = 0;

    while (1) {
        if (resp.type != MSG_DIR_REPLY) break;
        char *ptr = (char *)resp.payload;
        while (*ptr) {
            int is_d = (*ptr == '1') ? 1 : 0;
            while (*ptr && *ptr != '|') ptr++;
            if (*ptr == '|') ptr++;
            int cl = 0;
            while (*ptr >= '0' && *ptr <= '9') cl = cl*10 + (*ptr++ - '0');
            if (*ptr == '|') ptr++;
            char nm[64]; int ni = 0;
            while (*ptr && *ptr != '\n' && ni < 63) nm[ni++] = *ptr++;
            nm[ni] = 0;
            if (*ptr == '\n') ptr++;
            if (is_d && ni >= 4) {
                char up[5]; for (int i = 0; i < 4 && i < ni; i++) {
                    up[i] = nm[i]; if (up[i] >= 'a' && up[i] <= 'z') up[i] -= 32;
                } up[4] = 0;
                if (up[0]=='D'&&up[1]=='E'&&up[2]=='S'&&up[3]=='K')
                    desk_cl = (uint32_t)cl;
            }
        }
        if (resp.data1 == 0) break;
        qk_ipc_recv(&resp);
    }
    if (desk_cl == 0) return;

    memset(&req, 0, sizeof(req));
    req.type = MSG_DIR_REQ;
    req.data1 = desk_cl;
    qk_ipc_send_recv(storage_server_pid, &req, &resp);
    while (1) {
        if (resp.type != MSG_DIR_REPLY) break;
        char *ptr = (char *)resp.payload;
        while (*ptr && desk_entry_count < DESK_MAX_ICONS) {
            desk_entry_t *de = &desk_entries[desk_entry_count];
            de->is_dir = (*ptr == '1') ? 1 : 0;
            while (*ptr && *ptr != '|') ptr++;
            if (*ptr == '|') ptr++;
            int cl = 0;
            while (*ptr >= '0' && *ptr <= '9') cl = cl*10 + (*ptr++ - '0');
            de->cluster = (uint32_t)cl;
            if (*ptr == '|') ptr++;
            int ni = 0;
            while (*ptr && *ptr != '\n' && ni < 63) de->name[ni++] = *ptr++;
            de->name[ni] = 0;
            if (*ptr == '\n') ptr++;
            if (ni > 0 && de->name[0] != '.')
                desk_entry_count++;
        }
        if (resp.data1 == 0) break;
        qk_ipc_recv(&resp);
    }
}

static int desk_icon_id(const char *name)
{
    int len = strlen(name);
    if (len >= 4) {
        char ext[4];
        for (int i = 0; i < 3; i++) {
            ext[i] = name[len-3+i];
            if (ext[i] >= 'a' && ext[i] <= 'z') ext[i] -= 32;
        }
        ext[3] = 0;
        if (ext[0]=='B'&&ext[1]=='M'&&ext[2]=='P') return ICON_IMAGE;
        if (ext[0]=='P'&&ext[1]=='N'&&ext[2]=='G') return ICON_IMAGE;
        if (ext[0]=='J'&&ext[1]=='P'&&ext[2]=='G') return ICON_IMAGE;
        if (ext[0]=='T'&&ext[1]=='X'&&ext[2]=='T') return ICON_FILE_TEXT;
        if (ext[0]=='Q'&&ext[1]=='E'&&ext[2]=='X') return ICON_BINARY;
    }
    return ICON_FILE_TEXT;
}

static void render_desktop_icons(void)
{
    if (!desk_loaded) { desk_load(); return; }
    if (desk_entry_count == 0) return;
    int cols = ((int)ctx.width - DESK_START_X * 2) / (DESK_ICON_W + DESK_ICON_PAD);
    if (cols < 1) cols = 1;

    for (int i = 0; i < desk_entry_count; i++) {
        int col = i / ((int)(ctx.height - DESK_START_Y - 60) / (DESK_ICON_H + DESK_ICON_PAD));
        int row = i % ((int)(ctx.height - DESK_START_Y - 60) / (DESK_ICON_H + DESK_ICON_PAD));
        int x = DESK_START_X + col * (DESK_ICON_W + DESK_ICON_PAD);
        int y = DESK_START_Y + row * (DESK_ICON_H + DESK_ICON_PAD);

        if (i == desk_selected)
            gfx_rect_rounded_fill(&ctx, x, y, DESK_ICON_W, DESK_ICON_H, 8, 0x30FFFFFF);

        int icon = desk_entries[i].is_dir ? ICON_FOLDER : desk_icon_id(desk_entries[i].name);
        if (gui_icons)
            draw_icon_sized(&ctx, gui_icons, x + (DESK_ICON_W - 40) / 2,
                            y + 6, icon, 0xFFFFFFFF, 40);

        const char *name = desk_entries[i].name;
        int tw = 0;
        for (const char *p = name; *p; p++) tw += get_char_width(*p);
        int tx = x + (DESK_ICON_W - tw) / 2;
        if (tx < x + 2) tx = x + 2;
        draw_string_vector(&ctx, tx, y + 52, name, 0xFFDDDDDD);
    }
}

static void desk_handle_click(int mx2, int my2)
{
    int cols_h = ((int)ctx.height - DESK_START_Y - 60) / (DESK_ICON_H + DESK_ICON_PAD);
    if (cols_h < 1) cols_h = 1;
    int old_sel = desk_selected;
    desk_selected = -1;
    for (int i = 0; i < desk_entry_count; i++) {
        int col = i / cols_h;
        int row = i % cols_h;
        int x = DESK_START_X + col * (DESK_ICON_W + DESK_ICON_PAD);
        int y = DESK_START_Y + row * (DESK_ICON_H + DESK_ICON_PAD);
        if (mx2 >= x && mx2 < x + DESK_ICON_W && my2 >= y && my2 < y + DESK_ICON_H) {
            if (old_sel == i) {

                char path[128] = "/USR/DEFAULT/DESK/";
                int plen = strlen(path);
                strncpy(path + plen, desk_entries[i].name, 127 - plen);
                path[127] = 0;

                int icon = desk_icon_id(desk_entries[i].name);
                const char *app = 0;
                if (icon == ICON_IMAGE) app = "/PRG/IMGVIEW.QEX";
                else if (icon == ICON_FILE_TEXT) app = "/PRG/NOTEPAD.QEX";
                if (app) {
                    uint32_t sz = 0;
                    void *buf = load_file_from_storage(app, &sz);
                    if (buf && sz > 0) {
                        int pid = qk_spawn(buf, sz);
                        free(buf);
                        if (pid >= 0) {
                            message_t m;
                            memset(&m, 0, sizeof(m));
                            m.type = MSG_FILE_OPEN;
                            strncpy((char *)m.payload, path, sizeof(m.payload) - 1);
                            qk_sys_ipc_send(pid, &m);
                        }
                    }
                }
            } else {
                desk_selected = i;
            }
            return;
        }
    }
}

static void cache_terminal_binary(void)
{
    if (terminal_binary)
        return;
    uint32_t size = 0;
    void *buf = load_file_from_storage("/PRG/TERMINAL.QEX", &size);
    if (!buf || size == 0) {
        if (buf)
            free(buf);
        return;
    }
    terminal_binary = (uint8_t *)buf;
    terminal_binary_size = size;
}
static void cache_vkbd_binary(void)
{
    if (vkbd_binary)
        return;
    uint32_t size = 0;
    void *buf = load_file_from_storage("/PRG/VKBD.QEX", &size);
    if (!buf || size == 0) {
        if (buf)
            free(buf);
        return;
    }
    vkbd_binary = (uint8_t *)buf;
    vkbd_binary_size = size;
}
static void ensure_blur_buffer(int w, int h)
{
    if (blur_buffer && blur_buf_w == w && blur_buf_h == h)
        return;
    if (blur_buffer)
        free(blur_buffer);
    blur_buffer = (uint32_t *)malloc(sizeof(uint32_t) * w * h);
    blur_buf_w = w;
    blur_buf_h = h;
}
static void ensure_blur_snapshot(int w, int h)
{
    if (blur_snapshot && blur_snapshot_w == w && blur_snapshot_h == h)
        return;
    if (blur_snapshot)
        free(blur_snapshot);
    blur_snapshot = (uint32_t *)malloc(sizeof(uint32_t) * w * h);
    blur_snapshot_w = w;
    blur_snapshot_h = h;
}
static void invalidate_blur_snapshot(void)
{
    blur_snapshot_valid = 0;
}
static const int GAUSS_RADIUS = 4;
static const int gauss_kernel[9] = {1, 8, 28, 56, 70, 56, 28, 8, 1};
static const int gauss_weight_sum = 256;
static void blur_horizontal(uint32_t *src, uint32_t *dst, int w, int h)
{
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
static void blur_vertical(uint32_t *src, uint32_t *dst, int w, int h)
{
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
static void blur_buffer_inplace(uint32_t *buffer, int w, int h, int intensity)
{
    if (intensity <= 0)
        return;
    ensure_blur_buffer(w, h);
    int passes = clamp_int(intensity / 6 + 1, 1, 4);
    for (int i = 0; i < passes; i++) {
        blur_horizontal(buffer, blur_buffer, w, h);
        blur_vertical(blur_buffer, buffer, w, h);
    }
}
static void apply_screen_dim(gfx_context_t *context, uint32_t color, uint8_t alpha)
{
    if (!alpha)
        return;
    uint32_t total = context->width * context->height;
    for (uint32_t i = 0; i < total; i++) {
        context->buffer[i] = gfx_blend_colors(color, context->buffer[i], alpha);
    }
}
static void apply_brightness_overlay(void)
{
    uint8_t alpha = brightness_alpha_levels[brightness_level_index];
    if (!alpha)
        return;
    apply_screen_dim(&ctx, 0xFF000000, alpha);
}
static void cycle_brightness_level(void)
{
    brightness_level_index = (brightness_level_index + 1) % BRIGHTNESS_LEVEL_COUNT;
    char msg[32] = "Brightness ";
    char value[8];
    int_to_str(brightness_percent_levels[brightness_level_index], value, 10);
    strcat(msg, value);
    strcat(msg, "%");
    desktop_show_notification(msg);
}
static int point_in_rect(ui_rect_t rect, int px, int py)
{
    return (px >= rect.x && px < rect.x + rect.w && py >= rect.y && py < rect.y + rect.h);
}
static void layout_power_menu(void)
{
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
static int find_power_action_at(int x, int y)
{
    for (int i = 0; i < POWER_ACTION_COUNT; i++) {
        if (point_in_rect(power_actions[i].bounds, x, y))
            return i;
    }
    return -1;
}
static void close_power_overlay(void)
{
    power_overlay_visible = 0;
    power_overlay_hover = -1;
    invalidate_blur_snapshot();
}
static void open_power_overlay(void)
{
    power_overlay_visible = 1;
    power_overlay_hover = -1;
    layout_power_menu();
    invalidate_blur_snapshot();
}
static void exit_sleep_mode(void)
{
    sleep_mode_active = 0;
    invalidate_blur_snapshot();
}
static void enter_sleep_mode(void)
{
    sleep_mode_active = 1;
    close_power_overlay();
    invalidate_blur_snapshot();
}
static void trigger_power_action(power_action_type_t type)
{
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
static void launch_terminal_process(void)
{
    cache_terminal_binary();
    if (!terminal_binary || terminal_binary_size == 0) {
        desktop_show_notification("Terminal unavailable");
        return;
    }
    if (qk_spawn(terminal_binary, terminal_binary_size) < 0)
        desktop_show_notification("Failed to launch terminal");
}
static void request_vkbd_geom(void)
{
    if (vkbd_pid < 0)
        return;
    message_t req;
    memset(&req, 0, sizeof(req));
    req.type = MSG_VKBD_GEOM;
    qk_sys_ipc_send(vkbd_pid, &req);
}
static void apply_vkbd_geom(message_t *rep)
{
    int vis          = (int)rep->data1;
    int nx           = (int)rep->data2;
    int ny           = (int)rep->data3;
    uint32_t *pl     = (uint32_t *)rep->payload;
    int nw           = (int)pl[0];
    int nh           = (int)pl[1];
    uint32_t remote_buf = pl[2];
    if (!vis || nw <= 0 || nh <= 0 || remote_buf == 0)
        return;
    vkbd_pos_x = nx;
    vkbd_pos_y = ny;
    if (nw != vkbd_buf_w || nh != vkbd_buf_h || !vkbd_mapped_buf) {
        vkbd_buf_w = nw;
        vkbd_buf_h = nh;
        vkbd_mapped_buf = (uint32_t *)qk_map_foreign_mem(vkbd_pid, remote_buf,
                                                          (uint32_t)(nw * nh * 4));
    }
}
static void launch_vkbd_process(void)
{
    cache_vkbd_binary();
    if (!vkbd_binary || vkbd_binary_size == 0) {
        desktop_show_notification("Keyboard unavailable");
        return;
    }
    if (vkbd_pid >= 0) {
        message_t tmsg;
        memset(&tmsg, 0, sizeof(tmsg));
        tmsg.type = MSG_VKBD_TOGGLE;
        qk_sys_ipc_send(vkbd_pid, &tmsg);
        vkbd_is_visible = !vkbd_is_visible;
        vkbd_mapped_buf = 0;
        if (vkbd_is_visible)
            request_vkbd_geom();
        return;
    }
    int pid = qk_spawn(vkbd_binary, vkbd_binary_size);
    if (pid < 0) {
        desktop_show_notification("Failed to launch keyboard");
        return;
    }
    vkbd_pid = pid;
    vkbd_is_visible = 1;
    vkbd_mapped_buf = 0;
    uint32_t t = qk_get_uptime();
    while (qk_get_uptime() - t < 80)
        qk_yield();
    message_t tmsg;
    memset(&tmsg, 0, sizeof(tmsg));
    tmsg.type = MSG_VKBD_TOGGLE;
    qk_sys_ipc_send(vkbd_pid, &tmsg);
    request_vkbd_geom();
}
static int handle_mouse_for_ui(Event *e)
{
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
        if (point_in_rect(terminal_button_rect, e->x, e->y)) {
            launch_terminal_process();
            return 1;
        }
        if (point_in_rect(brightness_button_rect, e->x, e->y)) {
            cycle_brightness_level();
            return 1;
        }
        if (point_in_rect(keyboard_button_rect, e->x, e->y)) {
            launch_vkbd_process();
            return 1;
        }
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
void draw_pill(int x, int y, int w, int h)
{
    gfx_draw_shadow(&ctx, x, y, w, h, 7, 12);
    gfx_rect_rounded_fill(&ctx, x, y, w, h, 7, COLOR_WIN_HEADER);
}
void render_top_bar()
{
    int y = 10;
    int h = 35;
    uint32_t fg = COLOR_WIN_ACTIVE;
    uint32_t brightness_icon_color =
        (brightness_level_index == 0) ? fg : gfx_blend_colors(0xFFFFFFFF, fg, 160);
    int search_x = 10;
    int search_w = 35;
    draw_pill(search_x, y, search_w, h);
    draw_icon_sized(&ctx, gui_icons, search_x + (search_w - 18) / 2 - 1, y + (h - 18) / 2,
                    ICON_SEARCH, fg, 18);
    int clock_x = 1920 / 2 - 75;
    int clock_w = 150;
    int clock_y = y + 22;
    draw_pill(clock_x, y, clock_w, h);
    qk_time_t t;
    qk_get_time(&t);
    char time_str[40] = {0};
    char tmp[12];
    time_to_str(t.hour, tmp);
    strcat(time_str, tmp);
    strcat(time_str, ":");
    time_to_str(t.minute, tmp);
    strcat(time_str, tmp);
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char date_str[12] = {0};
    int m = (int)t.month;
    if (m >= 1 && m <= 12)
        strcat(date_str, months[m - 1]);
    else
        strcat(date_str, "---");
    strcat(date_str, " ");
    int_to_str(t.day, tmp, 10);
    strcat(date_str, tmp);
    draw_string_vector(&ctx, clock_x + 20, clock_y, date_str, fg);
    draw_string_vector(&ctx, clock_x + clock_w - 20 - get_string_width(time_str), clock_y, time_str, fg);
    int buttons_x = 1920 - 135;
    int buttons_w = 125;
    int buttons_y = 19;
    draw_pill(buttons_x, y, buttons_w, h);
    draw_icon_sized(&ctx, gui_icons, buttons_x + 9,  buttons_y, ICON_TERMINAL, fg, 18);
    draw_icon_sized(&ctx, gui_icons, buttons_x + 35, buttons_y, ICON_SUN, brightness_icon_color, 18);
    draw_icon_sized(&ctx, gui_icons, buttons_x + 67, buttons_y, ICON_KEYBOARD, fg, 18);
    draw_icon_sized(&ctx, gui_icons, buttons_x + 97, buttons_y, ICON_SHUTDOWN, fg, 18);
    terminal_button_rect.x = buttons_x + 10 - 4;
    terminal_button_rect.y = buttons_y - 4;
    terminal_button_rect.w = 28;
    terminal_button_rect.h = 28;
    brightness_button_rect.x = buttons_x + 36 - 4;
    brightness_button_rect.y = buttons_y - 4;
    brightness_button_rect.w = 28;
    brightness_button_rect.h = 28;
    keyboard_button_rect.x = buttons_x + 68 - 4;
    keyboard_button_rect.y = buttons_y - 4;
    keyboard_button_rect.w = 28;
    keyboard_button_rect.h = 28;
    power_button_rect.x = buttons_x + 98 - 4;
    power_button_rect.y = buttons_y - 4;
    power_button_rect.w = 28;
    power_button_rect.h = 28;
    int fps_x = buttons_x - 110;
    int fps_w = 100;
    int fps_y = y + 22;
    draw_pill(fps_x, y, fps_w, h);
    char fps_buf[16];
    int_to_str(current_fps, fps_buf, 10);
    draw_string_vector(&ctx, fps_x + 20, fps_y, "CPU", fg);
    draw_string_vector(&ctx, fps_x + 60, fps_y, fps_buf, fg);
}
void render_power_overlay(void)
{
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
        draw_icon_sized(&ctx, gui_icons, r.x + 7, r.y + (r.h - 18) / 2,
                        power_actions[i].icon_id, COLOR_WIN_ACTIVE, 18);
        draw_string_vector(&ctx, r.x + 36, r.y + r.h / 2 - 8, power_actions[i].label,
                           COLOR_WIN_ACTIVE);
    }
}
void render_sleep_screen(void)
{
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
static void render_desktop_notification(void)
{
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
void render_debug_overlay(void)
{
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
static void process_message(message_t *msg)
{
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
        if (vkbd_pid >= 0 && vkbd_is_visible &&
            (e->type == EVENT_MOUSE_MOVE || e->type == EVENT_MOUSE_DOWN ||
             e->type == EVENT_MOUSE_UP || e->type == EVENT_KEY_PRESS ||
             e->type == EVENT_KEY_RELEASE)) {
            qk_sys_ipc_send(vkbd_pid, msg);
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
                int px      = (int)msg->data1;
                int py      = (int)msg->data2;
                int count   = (int)msg->data3;
                int content_h = win->h - TITLE_BAR_HEIGHT;
                uint32_t *pixels = (uint32_t *)msg->payload;
                if (count > 128) count = 128;
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
            if (window_stack[i]->id == win_id &&
                window_stack[i]->owner_pid == msg->sender_pid) {
                win = window_stack[i];
                break;
            }
        }
        message_t reply;
        memset(&reply, 0, sizeof(reply));
        if (win && win->gfx_buffer) {
            int content_h = win->h - TITLE_BAR_HEIGHT;
            if (content_h < 1) content_h = 1;
            uint32_t buf_size = (uint32_t)win->w * (uint32_t)content_h * 4;
            reply.type  = MSG_WIN_BUFFER_INFO;
            reply.data1 = buf_size;
            reply.data2 = (uint32_t)win->w;
            reply.data3 = (uint32_t)content_h;
            ((uint32_t *)reply.payload)[0] = (uint32_t)win->gfx_buffer;
        } else {
            reply.type = MSG_ERROR;
        }
        qk_sys_ipc_send(msg->sender_pid, &reply);
    } else if (msg->type == MSG_WIN_DIRTY) {
        (void)msg;
    } else if (msg->type == MSG_CLIP_SET) {
        int len = (int)msg->data1;
        if (len > CLIP_MAX) len = CLIP_MAX;
        if (len < 0) len = 0;
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
    } else if (msg->type == MSG_PROCESS_TERMINATED) {
        int target_pid = msg->data1;
        for (int i = 0; i < window_count; i++) {
            if (window_stack[i]->owner_pid == target_pid) {
                close_window(window_stack[i]);
                i--;
            }
        }
        if (target_pid == vkbd_pid) {
            vkbd_pid = -1;
            vkbd_is_visible = 0;
            vkbd_mapped_buf = 0;
        }
    } else if (msg->type == MSG_VKBD_GEOM && msg->sender_pid == vkbd_pid) {
        apply_vkbd_geom(msg);
    } else if (msg->type == MSG_VKBD_KEY) {
        if (msg->data2 == 0xFFFFFFFFU) {
            debug_overlay_visible = !debug_overlay_visible;
        } else if (window_count > 0) {
            uint8_t sc      = (uint8_t)(msg->data1 & 0x7F);
            int     release = (msg->data1 & 0x80) != 0;
            message_t km;
            memset(&km, 0, sizeof(km));
            km.type  = MSG_KEY_EVENT;
            km.data1 = msg->data1;
            km.data2 = msg->data2;
            Event *e = (Event *)km.payload;
            e->type  = release ? EVENT_KEY_RELEASE : EVENT_KEY_PRESS;
            e->key   = sc;
            Window *top = window_stack[window_count - 1];
            qk_sys_ipc_send(top->owner_pid, &km);
        }
    }
}
static inline void copy_row_segment(uint8_t *dst_row, uint8_t *src_row, int start_px, int end_px)
{
    if (end_px <= start_px)
        return;
    memcpy(dst_row + start_px * 4, src_row + start_px * 4, (end_px - start_px) * 4);
}
static inline int rect_has_area(rect_i_t r)
{
    return (r.x1 > r.x0) && (r.y1 > r.y0);
}
static inline int rect_equals(rect_i_t a, rect_i_t b)
{
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}
static inline rect_i_t clamp_cursor_rect(int x, int y, int max_w, int max_h)
{
    rect_i_t r;
    r.x0 = clamp_int(x, 0, max_w);
    r.y0 = clamp_int(y, 0, max_h);
    r.x1 = clamp_int(x + CURSOR_W, 0, max_w);
    r.y1 = clamp_int(y + CURSOR_H, 0, max_h);
    return r;
}
static void copy_rect_from_ctx(rect_i_t r, uint8_t *dst, uint8_t *src, int dst_stride,
                               int src_stride)
{
    if (!rect_has_area(r))
        return;
    int w_bytes = (r.x1 - r.x0) * 4;
    for (int y = r.y0; y < r.y1; ++y)
        memcpy(dst + y * dst_stride + r.x0 * 4, src + y * src_stride + r.x0 * 4, w_bytes);
}
typedef struct {
    int x0, x1;
} interval_t;
static inline void add_interval_for_row(rect_i_t r, int y, interval_t *out, int *count, int max_w)
{
    if (y < r.y0 || y >= r.y1)
        return;
    int x0 = clamp_int(r.x0, 0, max_w);
    int x1 = clamp_int(r.x1, 0, max_w);
    if (x1 <= x0)
        return;
    for (int i = 0; i < *count; ++i)
        if (out[i].x0 == x0 && out[i].x1 == x1)
            return;
    out[*count].x0 = x0;
    out[*count].x1 = x1;
    (*count)++;
}
static void present_frame(void)
{
    if (!fb.buffer || !ctx.buffer)
        return;
    int screen_w = (int)ctx.width;
    int screen_h = (int)ctx.height;
    if (screen_w <= 0 || screen_h <= 0)
        return;
    rect_i_t new_cursor_rect = clamp_cursor_rect(mx, my, screen_w, screen_h);
    int new_cursor_valid = rect_has_area(new_cursor_rect);
    uint8_t *src = (uint8_t *)ctx.buffer;
    uint8_t *dst = (uint8_t *)fb.buffer;
    int src_stride = screen_w * 4;
    int dst_stride = (int)fb.pitch;
    int row_bytes = screen_w * 4;
    if (!prev_cursor_valid && !new_cursor_valid) {
        for (int y = 0; y < screen_h; ++y)
            memcpy(dst + y * dst_stride, src + y * src_stride, row_bytes);
        if (new_cursor_valid) {
            copy_rect_from_ctx(new_cursor_rect, dst, src, dst_stride, src_stride);
            gfx_draw_sprite(&fb_ctx, mx, my, CURSOR_W, CURSOR_H, cursor, cursor_palette);
            prev_cursor_rect = new_cursor_rect;
            prev_cursor_valid = 1;
        } else
            prev_cursor_valid = 0;
        return;
    }
    for (int y = 0; y < screen_h; ++y) {
        uint8_t *dst_row = dst + y * dst_stride;
        uint8_t *src_row = src + y * src_stride;
        interval_t intervals[2];
        int interval_count = 0;
        if (prev_cursor_valid)
            add_interval_for_row(prev_cursor_rect, y, intervals, &interval_count, screen_w);
        if (!prev_cursor_valid || !rect_equals(prev_cursor_rect, new_cursor_rect))
            if (new_cursor_valid)
                add_interval_for_row(new_cursor_rect, y, intervals, &interval_count, screen_w);
        if (interval_count == 0) {
            memcpy(dst_row, src_row, row_bytes);
            continue;
        }
        if (interval_count == 2 && intervals[0].x0 > intervals[1].x0) {
            interval_t tmp = intervals[0];
            intervals[0] = intervals[1];
            intervals[1] = tmp;
        }
        int cur = 0;
        for (int i = 0; i < interval_count; ++i) {
            if (cur < intervals[i].x0)
                copy_row_segment(dst_row, src_row, cur, intervals[i].x0);
            if (cur < intervals[i].x1)
                cur = intervals[i].x1;
        }
        if (cur < screen_w)
            copy_row_segment(dst_row, src_row, cur, screen_w);
    }
    if (prev_cursor_valid)
        copy_rect_from_ctx(prev_cursor_rect, dst, src, dst_stride, src_stride);
    if (!prev_cursor_valid || !rect_equals(prev_cursor_rect, new_cursor_rect))
        copy_rect_from_ctx(new_cursor_rect, dst, src, dst_stride, src_stride);
    gfx_draw_sprite(&fb_ctx, mx, my, CURSOR_W, CURSOR_H, cursor, cursor_palette);
    if (new_cursor_valid) {
        prev_cursor_rect = new_cursor_rect;
        prev_cursor_valid = 1;
    } else
        prev_cursor_valid = 0;
}
void render()
{
    if (wallpaper_data) {

        uint32_t wp_w = 1920;
        uint32_t copy_w = (fb.width < wp_w) ? fb.width : wp_w;
        for (uint32_t y = 0; y < fb.height; y++)
            memcpy(ctx.buffer + y * fb.width, wallpaper_data + y * wp_w, copy_w * 4);
    } else {
        memset32(ctx.buffer, UI_SURFACE_COLOR, (fb.width * fb.height));
    }
    draw_windows(&ctx);
    render_top_bar();
    if (power_overlay_visible || sleep_mode_active) {
        int w = (int)ctx.width;
        int h = (int)ctx.height;
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
        if (sleep_mode_active)
            memcpy32(ctx.buffer, blur_snapshot, w * h * 4);
    }
    render_power_overlay();
    render_sleep_screen();
    if (vkbd_pid >= 0 && vkbd_is_visible) {
        if (!vkbd_mapped_buf)
            request_vkbd_geom();
        if (vkbd_mapped_buf && vkbd_buf_w > 0 && vkbd_buf_h > 0) {
            int sw = (int)ctx.width;
            int sh = (int)ctx.height;
            for (int vy = 0; vy < vkbd_buf_h; vy++) {
                int dy = vkbd_pos_y + vy;
                if (dy < 0 || dy >= sh) continue;
                for (int vx = 0; vx < vkbd_buf_w; vx++) {
                    int dx = vkbd_pos_x + vx;
                    if (dx < 0 || dx >= sw) continue;
                    uint32_t px = vkbd_mapped_buf[vy * vkbd_buf_w + vx];
                    uint8_t  a  = (uint8_t)(px >> 24);
                    if (a == 0) continue;
                    if (a == 0xFF) {
                        ctx.buffer[dy * sw + dx] = px;
                    } else {
                        ctx.buffer[dy * sw + dx] =
                            gfx_blend_colors(px, ctx.buffer[dy * sw + dx], a);
                    }
                }
            }
        }
    }
    if (!sleep_mode_active)
        apply_brightness_overlay();
    render_desktop_notification();
    render_debug_overlay();
    present_frame();
    qk_gpu_flush();
    fps_counter++;
}
int main()
{
    void *icons_addr = 0, *font_addr = 0;
    qk_map_wallpaper((void **)&wallpaper_data);
    qk_map_icons_atlas(&icons_addr);
    if (icons_addr) {
        gui_icons = (AtlasFont *)malloc(sizeof(AtlasFont));
        font_atlas_init_for_ptr(gui_icons, (uint8_t *)icons_addr);
    }
    if (qk_map_framebuffer(&fb) != 0)
        return 1;
    fb_ctx = (gfx_context_t){fb.buffer, fb.width, fb.height, fb.pitch};
    ctx = (gfx_context_t){(uint32_t *)malloc(fb.width * fb.height * 4), fb.width, fb.height,
                          fb.width * 4};
    if (qk_map_font_atlas(&font_addr) == 0)
        font_atlas_init((uint8_t *)font_addr);
    window_manager_init();
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
