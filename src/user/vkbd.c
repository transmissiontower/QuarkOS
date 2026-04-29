// SPDX-License-Identifier: MIT
/*
 * src/user/vkbd.c - On-screen virtual keyboard with drag-to-move and modifier key support
 */
#include "lib/stdlib.h"
#include "user/lib/font_atlas.h"
#include "user/lib/gfx.h"
#include "user/lib/libapi.h"
#define VKBD_KEY_W   36
#define VKBD_KEY_H   34
#define VKBD_GAP      4
#define VKBD_PAD     10
#define VKBD_RADIUS   6
#define VKBD_ROWS     6
typedef struct { const char *label; uint8_t sc; int w; } VKey;
static const VKey vkbd_rowF[] = {
    {"Esc",0x01,44},{"F1",0x3B,0},{"F2",0x3C,0},{"F3",0x3D,0},{"F4",0x3E,0},
    {"F5",0x3F,0},{"F6",0x40,0},{"F7",0x41,0},{"F8",0x42,0},{"F9",0x43,0},
    {"F10",0x44,0},{"F11",0x57,0},{"F12",0x58,0},{"Del",0x53,44},{0,0,0}
};
static const VKey vkbd_row0[] = {
    {"`",0x29,0},{"1",0x02,0},{"2",0x03,0},{"3",0x04,0},{"4",0x05,0},
    {"5",0x06,0},{"6",0x07,0},{"7",0x08,0},{"8",0x09,0},{"9",0x0A,0},
    {"0",0x0B,0},{"-",0x0C,0},{"=",0x0D,0},{"<--",0x0E,54},{0,0,0}
};
static const VKey vkbd_row1[] = {
    {"Tab",0x0F,54},{"Q",0x10,0},{"W",0x11,0},{"E",0x12,0},{"R",0x13,0},
    {"T",0x14,0},{"Y",0x15,0},{"U",0x16,0},{"I",0x17,0},{"O",0x18,0},
    {"P",0x19,0},{"[",0x1A,0},{"]",0x1B,0},{"\\",0x2B,40},{0,0,0}
};
static const VKey vkbd_row2[] = {
    {"Caps",0x3A,62},{"A",0x1E,0},{"S",0x1F,0},{"D",0x20,0},{"F",0x21,0},
    {"G",0x22,0},{"H",0x23,0},{"J",0x24,0},{"K",0x25,0},{"L",0x26,0},
    {";",0x27,0},{"'",0x28,0},{"Enter",0x1C,68},{0,0,0}
};
static const VKey vkbd_row3[] = {
    {"Shift",0x2A,80},{"Z",0x2C,0},{"X",0x2D,0},{"C",0x2E,0},{"V",0x2F,0},
    {"B",0x30,0},{"N",0x31,0},{"M",0x32,0},{",",0x33,0},{".",0x34,0},
    {"/",0x35,0},{"Shift",0x36,80},{0,0,0}
};
static const VKey vkbd_row4[] = {
    {"Ctrl",0x1D,60},{"Alt",0x38,50},{"Space",0x39,280},{"Alt",0x38,50},{"Ctrl",0x1D,60},{0,0,0}
};
static const VKey *vkbd_rows[VKBD_ROWS] = {
    vkbd_rowF, vkbd_row0, vkbd_row1, vkbd_row2, vkbd_row3, vkbd_row4
};
static int vkbd_visible  = 0;
static int vkbd_x        = 300;
static int vkbd_y        = 600;
static int vkbd_dragging = 0;
static int vkbd_drag_ox  = 0;
static int vkbd_drag_oy  = 0;
static int vkbd_caps     = 0;
static int vkbd_shift    = 0;
static uint8_t vkbd_pressed_sc = 0;
static int screen_w = 1024;
static int screen_h = 768;
#define VKBD_MOD_SHIFT 0x01
#define VKBD_MOD_CTRL  0x02
#define VKBD_MOD_ALT   0x04
#define VKBD_MOD_CAPS  0x08
#define COLOR_PANEL    0xFF1E1E1E
#define COLOR_KEY      0xFF2E2E2E
#define COLOR_KEY_LIT  0xFF505050
#define COLOR_KEY_ON   0xFF404040
#define COLOR_TEXT     0xFFFFFFFF
#define COLOR_DRAG     0xFF242424
static uint32_t      *vkbd_buf = 0;
static gfx_context_t  vkbd_ctx;
static int vkbd_panel_w(void)
{
    int max_w = 0;
    for (int r = 0; r < VKBD_ROWS; r++) {
        int row_w = VKBD_PAD;
        for (int k = 0; vkbd_rows[r][k].label; k++) {
            int kw = vkbd_rows[r][k].w ? vkbd_rows[r][k].w : VKBD_KEY_W;
            row_w += kw + VKBD_GAP;
        }
        row_w += VKBD_PAD - VKBD_GAP;
        if (row_w > max_w) max_w = row_w;
    }
    return max_w;
}
static int vkbd_panel_h(void)
{
    return 24 + VKBD_PAD + VKBD_ROWS * (VKBD_KEY_H + VKBD_GAP) + VKBD_PAD;
}
static void render_vkbd(void)
{
    if (!vkbd_visible || !vkbd_buf) return;
    int pw = vkbd_panel_w();
    int ph = vkbd_panel_h();
    memset(vkbd_buf, 0, (uint32_t)(pw * ph * 4));
    gfx_rect_rounded_fill(&vkbd_ctx, 0, 0, pw, ph, 10, COLOR_PANEL);
    gfx_rect_rounded_fill(&vkbd_ctx, 0, 0, pw, 24, 10, COLOR_DRAG);
    gfx_rect_fill(&vkbd_ctx, 0, 14, pw, 10, COLOR_DRAG);
    int ky = 24 + VKBD_PAD;
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
            for (int ci = 0; lbl[ci]; ci++) lw += get_char_width(lbl[ci]);
            int fh = get_font_height();
            draw_string_vector(&vkbd_ctx, kx + (kw - lw) / 2, ky + (VKBD_KEY_H - fh) / 2 + 15,
                               lbl, COLOR_TEXT);
            kx += kw + VKBD_GAP;
        }
        ky += VKBD_KEY_H + VKBD_GAP;
    }
}
static void push_geom_to_comp(int comp_pid)
{
    int pw = vkbd_panel_w();
    int ph = vkbd_panel_h();
    message_t reply;
    memset(&reply, 0, sizeof(reply));
    reply.type  = MSG_VKBD_GEOM;
    reply.data1 = (uint32_t)vkbd_visible;
    reply.data2 = (uint32_t)vkbd_x;
    reply.data3 = (uint32_t)vkbd_y;
    uint32_t *pl = (uint32_t *)reply.payload;
    pl[0] = (uint32_t)pw;
    pl[1] = (uint32_t)ph;
    pl[2] = (uint32_t)vkbd_buf;
    qk_sys_ipc_send(comp_pid, &reply);
}
static void send_key_to_comp(int comp_pid, uint8_t sc, int release)
{
    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    msg.type  = MSG_VKBD_KEY;
    msg.data1 = (uint32_t)sc | (release ? 0x80U : 0);
    uint32_t mods = 0;
    if (vkbd_shift) mods |= VKBD_MOD_SHIFT;
    if (vkbd_caps)  mods |= VKBD_MOD_CAPS;
    msg.data2 = mods;
    qk_sys_ipc_send(comp_pid, &msg);
}
static void handle_mouse(Event *e, int comp_pid)
{
    if (!vkbd_visible) return;
    int pw = vkbd_panel_w();
    int ph = vkbd_panel_h();
    if (vkbd_dragging) {
        if (e->type == EVENT_MOUSE_MOVE || e->type == EVENT_MOUSE_DOWN) {
            vkbd_x = e->x - vkbd_drag_ox;
            vkbd_y = e->y - vkbd_drag_oy;
            if (vkbd_x < 0) vkbd_x = 0;
            if (vkbd_y < 0) vkbd_y = 0;
            if (vkbd_x + pw > screen_w) vkbd_x = screen_w - pw;
            if (vkbd_y + ph > screen_h) vkbd_y = screen_h - ph;
            push_geom_to_comp(comp_pid);
        }
        if (e->type == EVENT_MOUSE_UP) {
            vkbd_dragging = 0;
            push_geom_to_comp(comp_pid);
        }
        return;
    }
    if (e->x < vkbd_x || e->x >= vkbd_x + pw ||
        e->y < vkbd_y || e->y >= vkbd_y + ph)
        return;
    if (e->type == EVENT_MOUSE_UP) {
        vkbd_pressed_sc = 0;
        return;
    }
    if (e->type == EVENT_MOUSE_DOWN && e->button == MOUSE_BTN_LEFT) {
        if (e->y < vkbd_y + 24) {
            vkbd_dragging = 1;
            vkbd_drag_ox  = e->x - vkbd_x;
            vkbd_drag_oy  = e->y - vkbd_y;
            return;
        }
        int ky = vkbd_y + 24 + VKBD_PAD;
        for (int r = 0; r < VKBD_ROWS; r++) {
            int kx = vkbd_x + VKBD_PAD;
            for (int k = 0; vkbd_rows[r][k].label; k++) {
                int kw = vkbd_rows[r][k].w ? vkbd_rows[r][k].w : VKBD_KEY_W;
                if (e->x >= kx && e->x < kx + kw &&
                    e->y >= ky && e->y < ky + VKBD_KEY_H) {
                    uint8_t sc = vkbd_rows[r][k].sc;
                    vkbd_pressed_sc = sc;
                    if (sc == 0x3A) { vkbd_caps = !vkbd_caps; return; }
                    if (sc == 0x2A || sc == 0x36) return;
                    if (sc == 0x58) {
                        message_t m;
                        memset(&m, 0, sizeof(m));
                        m.type  = MSG_VKBD_KEY;
                        m.data1 = 0x58;
                        m.data2 = 0xFFFFFFFFU;
                        qk_sys_ipc_send(comp_pid, &m);
                        return;
                    }
                    send_key_to_comp(comp_pid, sc, 0);
                    send_key_to_comp(comp_pid, sc, 1);
                    return;
                }
                kx += kw + VKBD_GAP;
            }
            ky += VKBD_KEY_H + VKBD_GAP;
        }
    }
}
void main(void)
{
    void *font_addr = 0;
    fb_info_t fb;
    if (qk_map_framebuffer(&fb) == 0) {
        screen_w = (int)fb.width;
        screen_h = (int)fb.height;
    }
    if (qk_map_font_atlas(&font_addr) == 0)
        font_atlas_init((uint8_t *)font_addr);
    int pw = vkbd_panel_w();
    int ph = vkbd_panel_h();
    vkbd_buf = (uint32_t *)malloc((uint32_t)(pw * ph * 4));
    if (!vkbd_buf) return;
    memset(vkbd_buf, 0, (uint32_t)(pw * ph * 4));
    vkbd_ctx = (gfx_context_t){vkbd_buf, (uint32_t)pw, (uint32_t)ph, (uint32_t)pw * 4};
    int comp_pid = -1;
    while (comp_pid <= 0)
        comp_pid = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    message_t msg;
    while (1) {
        if (qk_ipc_recv(&msg) != 0)
            continue;
        if (msg.type == MSG_VKBD_TOGGLE) {
            vkbd_visible = !vkbd_visible;
            vkbd_pressed_sc = 0;
            render_vkbd();
        } else if (msg.type == MSG_MOUSE_EVENT) {
            Event *e = (Event *)msg.payload;
            handle_mouse(e, comp_pid);
            render_vkbd();
        } else if (msg.type == MSG_KEY_EVENT) {
            Event *e = (Event *)msg.payload;
            if (e->key == 0x2A || e->key == 0x36)
                vkbd_shift = (e->type == EVENT_KEY_PRESS) ? 1 : 0;
            if (e->key == 0x3A && e->type == EVENT_KEY_PRESS)
                vkbd_caps = !vkbd_caps;
            if (e->type == EVENT_KEY_PRESS)
                vkbd_pressed_sc = (uint8_t)(e->key & 0x7F);
            else if (e->type == EVENT_KEY_RELEASE)
                vkbd_pressed_sc = 0;
            render_vkbd();
        } else if (msg.type == MSG_VKBD_GEOM) {
            push_geom_to_comp(comp_pid);
        }
    }
}
