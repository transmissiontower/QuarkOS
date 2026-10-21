#include "stdlib.h"
#include "user/libui/font_atlas.h"
#include "user/libui/gfx.h"
#include "user/libc/libapi.h"
#define VKBD_KEY_W 36
#define VKBD_KEY_H 34
#define VKBD_GAP 4
#define VKBD_PAD 10
#define VKBD_RADIUS 6
#define VKBD_ROWS 6
typedef struct {
    const char *label;
    uint8_t sc;
    int w;
} VKey;
static const VKey vkbd_rowF[] = {
    {"Esc", 0x01, 44}, {"F1", 0x3B, 0},  {"F2", 0x3C, 0},  {"F3", 0x3D, 0},   {"F4", 0x3E, 0},
    {"F5", 0x3F, 0},   {"F6", 0x40, 0},  {"F7", 0x41, 0},  {"F8", 0x42, 0},   {"F9", 0x43, 0},
    {"F10", 0x44, 0},  {"F11", 0x57, 0}, {"F12", 0x58, 0}, {"Del", 0x53, 44}, {0, 0, 0}};
static const VKey vkbd_row0[] = {{"`", 0x29, 0}, {"1", 0x02, 0},    {"2", 0x03, 0}, {"3", 0x04, 0},
                                 {"4", 0x05, 0}, {"5", 0x06, 0},    {"6", 0x07, 0}, {"7", 0x08, 0},
                                 {"8", 0x09, 0}, {"9", 0x0A, 0},    {"0", 0x0B, 0}, {"-", 0x0C, 0},
                                 {"=", 0x0D, 0}, {"<--", 0x0E, 54}, {0, 0, 0}};
static const VKey vkbd_row1[] = {
    {"Tab", 0x0F, 54}, {"Q", 0x10, 0}, {"W", 0x11, 0}, {"E", 0x12, 0},   {"R", 0x13, 0},
    {"T", 0x14, 0},    {"Y", 0x15, 0}, {"U", 0x16, 0}, {"I", 0x17, 0},   {"O", 0x18, 0},
    {"P", 0x19, 0},    {"[", 0x1A, 0}, {"]", 0x1B, 0}, {"\\", 0x2B, 40}, {0, 0, 0}};
static const VKey vkbd_row2[] = {
    {"Caps", 0x3A, 62}, {"A", 0x1E, 0}, {"S", 0x1F, 0},      {"D", 0x20, 0}, {"F", 0x21, 0},
    {"G", 0x22, 0},     {"H", 0x23, 0}, {"J", 0x24, 0},      {"K", 0x25, 0}, {"L", 0x26, 0},
    {";", 0x27, 0},     {"'", 0x28, 0}, {"Enter", 0x1C, 68}, {0, 0, 0}};
static const VKey vkbd_row3[] = {
    {"Shift", 0x2A, 80}, {"Z", 0x2C, 0},      {"X", 0x2D, 0}, {"C", 0x2E, 0}, {"V", 0x2F, 0},
    {"B", 0x30, 0},      {"N", 0x31, 0},      {"M", 0x32, 0}, {",", 0x33, 0}, {".", 0x34, 0},
    {"/", 0x35, 0},      {"Shift", 0x36, 80}, {0, 0, 0}};
static const VKey vkbd_row4[] = {{"Ctrl", 0x1D, 60}, {"Alt", 0x38, 50},  {"Space", 0x39, 280},
                                 {"Alt", 0x38, 50},  {"Ctrl", 0x1D, 60}, {0, 0, 0}};
static const VKey *vkbd_rows[VKBD_ROWS] = {vkbd_rowF, vkbd_row0, vkbd_row1,
                                           vkbd_row2, vkbd_row3, vkbd_row4};
static int vkbd_caps = 0;
static int vkbd_shift = 0;
static uint8_t vkbd_pressed_sc = 0;
#define VKBD_MOD_SHIFT 0x01
#define VKBD_MOD_CTRL 0x02
#define VKBD_MOD_ALT 0x04
#define VKBD_MOD_CAPS 0x08
#define COLOR_PANEL 0xFF1E1E1E
#define COLOR_KEY 0xFF2E2E2E
#define COLOR_KEY_LIT 0xFF505050
#define COLOR_KEY_ON 0xFF404040
#define COLOR_TEXT 0xFFFFFFFF
#define COLOR_DRAG 0xFF242424
static uint32_t *vkbd_buf = 0;
static gfx_context_t vkbd_ctx;
static int comp_pid = -1;
static int win_id = -1;
static int vkbd_panel_w(void) {
    int max_w = 0;
    for (int r = 0; r < VKBD_ROWS; r++) {
        int row_w = VKBD_PAD;
        for (int k = 0; vkbd_rows[r][k].label; k++) {
            int kw = vkbd_rows[r][k].w ? vkbd_rows[r][k].w : VKBD_KEY_W;
            row_w += kw + VKBD_GAP;
        }
        row_w += VKBD_PAD - VKBD_GAP;
        if (row_w > max_w)
            max_w = row_w;
    }
    return max_w;
}
static int vkbd_panel_h(void) {
    return VKBD_PAD + VKBD_ROWS * (VKBD_KEY_H + VKBD_GAP) + VKBD_PAD;
}
static void render_vkbd(void) {
    if (!vkbd_buf)
        return;
    int pw = vkbd_panel_w();
    int ph = vkbd_panel_h();
    memset(vkbd_buf, 0, (uint32_t)(pw * ph * 4));
    gfx_rect_fill(&vkbd_ctx, 0, 0, pw, ph, COLOR_PANEL);
    int ky = VKBD_PAD;
    for (int r = 0; r < VKBD_ROWS; r++) {
        int kx = VKBD_PAD;
        for (int k = 0; vkbd_rows[r][k].label; k++) {
            int kw = vkbd_rows[r][k].w ? vkbd_rows[r][k].w : VKBD_KEY_W;
            uint8_t sc = vkbd_rows[r][k].sc;
            uint32_t key_col;
            if (sc == vkbd_pressed_sc && sc != 0)
                key_col = COLOR_KEY_LIT;
            else if ((sc == 0x3A && vkbd_caps) || ((sc == 0x2A || sc == 0x36) && vkbd_shift))
                key_col = COLOR_KEY_ON;
            else
                key_col = COLOR_KEY;
            gfx_rect_rounded_fill(&vkbd_ctx, kx, ky, kw, VKBD_KEY_H, VKBD_RADIUS, key_col);
            const char *lbl = vkbd_rows[r][k].label;
            char lbl_tmp[4];
            if (lbl[0] >= 'A' && lbl[0] <= 'Z' && lbl[1] == '\0') {
                int upper = vkbd_caps ^ vkbd_shift;
                lbl_tmp[0] = upper ? lbl[0] : (char)(lbl[0] + 32);
                lbl_tmp[1] = '\0';
                lbl = lbl_tmp;
            }
            int lw = 0;
            for (int ci = 0; lbl[ci]; ci++)
                lw += get_char_width(lbl[ci]);
            int fh = get_font_height();
            draw_string_vector(&vkbd_ctx, kx + (kw - lw) / 2, ky + (VKBD_KEY_H - fh) / 2 + 15, lbl,
                               COLOR_TEXT);
            kx += kw + VKBD_GAP;
        }
        ky += VKBD_KEY_H + VKBD_GAP;
    }
}
static void send_key_to_system(uint8_t sc, int release) {
    Event event = {.type = release ? EVENT_KEY_RELEASE : EVENT_KEY_PRESS, .key = sc};
    uint32_t modifiers = (vkbd_shift ? VKBD_MOD_SHIFT : 0) | (vkbd_caps ? VKBD_MOD_CAPS : 0);
    qk_input_inject(&event, modifiers);
}
static void handle_mouse(Event *e) {
    int local_x = e->x;
    int local_y = e->y;
    int pw = vkbd_panel_w();
    int ph = vkbd_panel_h();
    if (local_x < 0 || local_x >= pw || local_y < 0 || local_y >= ph)
        return;
    if (e->type == EVENT_MOUSE_UP) {
        vkbd_pressed_sc = 0;
        return;
    }
    if (e->type != EVENT_MOUSE_DOWN || e->button != MOUSE_BTN_LEFT)
        return;
    int ky = VKBD_PAD;
    for (int r = 0; r < VKBD_ROWS; r++) {
        int kx = VKBD_PAD;
        for (int k = 0; vkbd_rows[r][k].label; k++) {
            int kw = vkbd_rows[r][k].w ? vkbd_rows[r][k].w : VKBD_KEY_W;
            if (local_x >= kx && local_x < kx + kw && local_y >= ky && local_y < ky + VKBD_KEY_H) {
                uint8_t sc = vkbd_rows[r][k].sc;
                vkbd_pressed_sc = sc;
                if (sc == 0x3A) {
                    vkbd_caps = !vkbd_caps;
                    return;
                }
                if (sc == 0x2A || sc == 0x36) {
                    vkbd_shift = !vkbd_shift;
                    return;
                }
                send_key_to_system(sc, 0);
                send_key_to_system(sc, 1);
                return;
            }
            kx += kw + VKBD_GAP;
        }
        ky += VKBD_KEY_H + VKBD_GAP;
    }
}

int main(void) {
    comp_pid = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    if (comp_pid <= 0)
        return 1;
    font_atlas_load_from_disk("/SYS/DAT/INTER.TTF");
    int pw = vkbd_panel_w();
    int ph = vkbd_panel_h();
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_WIN_CREATE;
    msg.data1 = (uint32_t)pw;
    msg.data2 = (uint32_t)(ph + 34);
    strncpy((char *)msg.payload, "Virtual Keyboard", sizeof(msg.payload) - 1);
    qk_sys_ipc_send(comp_pid, &msg);
    for (;;) {
        if (qk_ipc_recv(&msg) == 0 && msg.type == MSG_WIN_CREATED)
            break;
    }
    win_id = (int)msg.data1;
    int width = 0, height = 0;
    vkbd_buf = qk_win_map_buffer(comp_pid, win_id, &width, &height);
    if (!vkbd_buf || width <= 0 || height <= 0)
        return 1;
    vkbd_ctx = (gfx_context_t){vkbd_buf, (uint32_t)width, (uint32_t)height, (uint32_t)width * 4};
    render_vkbd();
    while (1) {
        if (qk_ipc_recv(&msg) != 0)
            continue;
        if (msg.type == MSG_MOUSE_EVENT) {
            handle_mouse((Event *)msg.payload);
            render_vkbd();
        } else if (msg.type == MSG_KEY_EVENT) {
            Event *event = (Event *)msg.payload;
            if (event->key == 0x2A || event->key == 0x36)
                vkbd_shift = event->type == EVENT_KEY_PRESS;
            if (event->key == 0x3A && event->type == EVENT_KEY_PRESS)
                vkbd_caps = !vkbd_caps;
            vkbd_pressed_sc = event->type == EVENT_KEY_PRESS ? (uint8_t)(event->key & 0x7F) : 0;
            render_vkbd();
        } else if (msg.type == MSG_WIN_RESIZED) {
            uint32_t *new_buffer = qk_win_map_buffer(comp_pid, win_id, &width, &height);
            if (new_buffer && width > 0 && height > 0) {
                vkbd_buf = new_buffer;
                vkbd_ctx = (gfx_context_t){vkbd_buf, (uint32_t)width, (uint32_t)height,
                                           (uint32_t)width * 4};
                render_vkbd();
            }
        }
    }
    return 0;
}
