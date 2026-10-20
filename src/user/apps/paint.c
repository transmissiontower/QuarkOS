#include "abi/events.h"
#include "abi/ipc.h"
#include "stdbool.h"
#include "stdlib.h"
#include "user/libui/font_atlas.h"
#include "user/libui/gfx.h"
#include "user/libc/libapi.h"
#include "user/libui/window.h"

#define WIN_W 900
#define WIN_H 600
#define TOOLBAR_H 48

#define COL_TOOLBAR 0xFF242424u
#define COL_SEP 0xFF333333u
#define COL_TEXT 0xFFAAAAAAu
#define COL_ACCENT 0xFFCCCCCCu
#define COL_BTN_IDLE 0xFF2A2A2Au
#define COL_BTN_SEL 0xFF404040u
#define COL_CANVAS 0xFFFFFFFFu

static int comp_pid = -1;
static uint32_t *gfx = 0;
static int gfx_w = 0;
static int gfx_h = 0;
static int win_id = -1;

typedef enum { TOOL_PENCIL, TOOL_ERASER, TOOL_FILL } Tool;
static Tool cur_tool = TOOL_PENCIL;
static int brush_idx = 1;
static uint32_t draw_color = 0xFF000000u;
static bool drawing = false;
static int last_x = -1;
static int last_y = -1;

static const int brush_r[] = {0, 1, 4, 8};
#define BRUSH_COUNT 4

static const uint32_t palette[] = {
    0xFF000000u, 0xFF404040u, 0xFF808080u, 0xFFFFFFFFu, 0xFFFF0000u, 0xFF00AA00u,
    0xFF0000FFu, 0xFF00CCCCu, 0xFFCC00CCu, 0xFFFFFF00u, 0xFFFF8800u, 0xFF8800CCu,
    0xFF884422u, 0xFFFF88AAu, 0xFF88CCFFu, 0xFF88FFAAu,
};
#define PALETTE_COUNT 16

static int str_px(const char *s) {
    int w = 0;
    for (; *s; s++)
        w += get_char_width(*s);
    return w;
}

static void fill_rect(int x, int y, int w, int h, uint32_t c) {
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
    if (x + w > gfx_w)
        w = gfx_w - x;
    if (y + h > gfx_h)
        h = gfx_h - y;
    if (w <= 0 || h <= 0)
        return;
    for (int row = 0; row < h; row++) {
        uint32_t *p = gfx + (y + row) * gfx_w + x;
        for (int k = 0; k < w; k++)
            p[k] = c;
    }
}

static void fill_rd(int x, int y, int w, int h, int r, uint32_t c) {
    gfx_context_t ctx = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
    gfx_rect_rounded_fill(&ctx, x, y, w, h, r, c);
}

static void dstr(int x, int y, const char *s, uint32_t c) {
    gfx_context_t ctx = {gfx, (uint32_t)gfx_w, (uint32_t)gfx_h, (uint32_t)gfx_w};
    draw_string_vector(&ctx, x, y + 15, s, c);
}

static void put_px(int x, int y, uint32_t c) {
    if (x < 0 || y < TOOLBAR_H || x >= gfx_w || y >= gfx_h)
        return;
    gfx[y * gfx_w + x] = c;
}

static void draw_dot(int cx, int cy, int r, uint32_t c) {
    if (r == 0) {
        put_px(cx, cy, c);
        return;
    }
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r)
                put_px(cx + dx, cy + dy, c);
}

static void draw_line(int x0, int y0, int x1, int y1, int r, uint32_t c) {
    int dx = x1 - x0;
    if (dx < 0)
        dx = -dx;
    int dy = y1 - y0;
    if (dy < 0)
        dy = -dy;
    int sx = (x1 >= x0) ? 1 : -1;
    int sy = (y1 >= y0) ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        draw_dot(x0, y0, r, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void flood_fill(int sx, int sy, uint32_t target, uint32_t rep) {
    if (target == rep)
        return;
    if (sx < 0 || sy < TOOLBAR_H || sx >= gfx_w || sy >= gfx_h)
        return;
    if (gfx[sy * gfx_w + sx] != target)
        return;

#define FF_MAX 16384
    static int qx[FF_MAX], qy[FF_MAX];
    int top = 0;
    qx[top] = sx;
    qy[top] = sy;
    top++;

    while (top > 0) {
        top--;
        int x = qx[top], y = qy[top];
        if (x < 0 || y < TOOLBAR_H || x >= gfx_w || y >= gfx_h)
            continue;
        if (gfx[y * gfx_w + x] != target)
            continue;

        int x1 = x, x2 = x;
        while (x1 > 0 && gfx[y * gfx_w + x1 - 1] == target)
            x1--;
        while (x2 < gfx_w - 1 && gfx[y * gfx_w + x2 + 1] == target)
            x2++;
        for (int i = x1; i <= x2; i++)
            gfx[y * gfx_w + i] = rep;

        for (int i = x1; i <= x2 && top < FF_MAX - 1; i++) {
            if (y > TOOLBAR_H && gfx[(y - 1) * gfx_w + i] == target) {
                qx[top] = i;
                qy[top] = y - 1;
                top++;
            }
            if (y < gfx_h - 1 && gfx[(y + 1) * gfx_w + i] == target) {
                qx[top] = i;
                qy[top] = y + 1;
                top++;
            }
        }
    }
}

static void clear_canvas(void) {
    fill_rect(0, TOOLBAR_H, gfx_w, gfx_h - TOOLBAR_H, COL_CANVAS);
}

#define BTN_Y 6
#define BTN_H (TOOLBAR_H - 12)
#define BTN_PAD 14

typedef struct {
    int x, w;
} Btn;

static int compute_layout(Btn tools[3], Btn brushes[4]) {
    const char *tlabels[] = {"Pen", "Erase", "Fill"};
    int x = 8;
    for (int i = 0; i < 3; i++) {
        tools[i].x = x;
        tools[i].w = str_px(tlabels[i]) + BTN_PAD;
        x += tools[i].w + 4;
    }
    x += 10;
    const char *slabels[] = {"S", "M", "L", "XL"};
    for (int i = 0; i < BRUSH_COUNT; i++) {
        brushes[i].x = x;
        brushes[i].w = str_px(slabels[i]) + BTN_PAD;
        x += brushes[i].w + 4;
    }
    return x + 10;
}

static void render_toolbar(void) {
    if (!gfx || !user_font)
        return;
    int fh = get_font_height();
    int ty = (TOOLBAR_H - fh) / 2;

    fill_rect(0, 0, gfx_w, TOOLBAR_H, COL_TOOLBAR);
    fill_rect(0, TOOLBAR_H - 1, gfx_w, 1, COL_SEP);

    Btn tb[3], bb[4];
    int palette_x = compute_layout(tb, bb);

    const char *tlabels[] = {"Pen", "Erase", "Fill"};
    Tool ttools[] = {TOOL_PENCIL, TOOL_ERASER, TOOL_FILL};
    for (int i = 0; i < 3; i++) {
        bool sel = (cur_tool == ttools[i]);
        fill_rd(tb[i].x, BTN_Y, tb[i].w, BTN_H, 4, sel ? COL_BTN_SEL : COL_BTN_IDLE);
        dstr(tb[i].x + BTN_PAD / 2, ty, tlabels[i], sel ? COL_ACCENT : COL_TEXT);
    }
    fill_rect(tb[2].x + tb[2].w + 6, 8, 1, TOOLBAR_H - 16, COL_SEP);

    const char *slabels[] = {"S", "M", "L", "XL"};
    for (int i = 0; i < BRUSH_COUNT; i++) {
        bool sel = (brush_idx == i);
        fill_rd(bb[i].x, BTN_Y, bb[i].w, BTN_H, 4, sel ? COL_BTN_SEL : COL_BTN_IDLE);
        dstr(bb[i].x + BTN_PAD / 2, ty, slabels[i], sel ? COL_ACCENT : COL_TEXT);
    }
    fill_rect(bb[3].x + bb[3].w + 6, 8, 1, TOOLBAR_H - 16, COL_SEP);

    int sw = BTN_H;
    int x = palette_x;
    for (int i = 0; i < PALETTE_COUNT; i++) {
        fill_rect(x, BTN_Y, sw, sw, palette[i]);
        if (palette[i] == draw_color) {
            fill_rect(x - 2, BTN_Y - 2, sw + 4, 2, COL_ACCENT);
            fill_rect(x - 2, BTN_Y + sw, sw + 4, 2, COL_ACCENT);
            fill_rect(x - 2, BTN_Y - 2, 2, sw + 4, COL_ACCENT);
            fill_rect(x + sw, BTN_Y - 2, 2, sw + 4, COL_ACCENT);
        }
        x += sw + 3;
    }
}

static void handle_toolbar_click(int mx, int my) {
    if (my < BTN_Y || my >= BTN_Y + BTN_H)
        return;

    Btn tb[3], bb[4];
    int palette_x = compute_layout(tb, bb);

    Tool ttools[] = {TOOL_PENCIL, TOOL_ERASER, TOOL_FILL};
    for (int i = 0; i < 3; i++) {
        if (mx >= tb[i].x && mx < tb[i].x + tb[i].w) {
            cur_tool = ttools[i];
            render_toolbar();
            qk_win_dirty(comp_pid);
            return;
        }
    }
    for (int i = 0; i < BRUSH_COUNT; i++) {
        if (mx >= bb[i].x && mx < bb[i].x + bb[i].w) {
            brush_idx = i;
            render_toolbar();
            qk_win_dirty(comp_pid);
            return;
        }
    }

    int sw = BTN_H;
    int x = palette_x;
    for (int i = 0; i < PALETTE_COUNT; i++) {
        if (mx >= x && mx < x + sw) {
            draw_color = palette[i];
            render_toolbar();
            qk_win_dirty(comp_pid);
            return;
        }
        x += sw + 3;
    }
}

int main(void) {
    comp_pid = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    if (comp_pid < 0)
        return 1;

    void *font_addr = 0;
    font_atlas_load_from_disk("/SYS/DAT/INTER.TTF");

    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_WIN_CREATE;
    msg.data1 = WIN_W;
    msg.data2 = WIN_H;
    strncpy((char *)msg.payload, "Paint", 63);
    qk_sys_ipc_send(comp_pid, &msg);

    while (1) {
        qk_ipc_recv(&msg);
        if (msg.type == MSG_WIN_CREATED)
            break;
    }
    win_id = (int)msg.data1;
    gfx = (uint32_t *)qk_win_map_buffer(comp_pid, win_id, &gfx_w, &gfx_h);

    clear_canvas();
    render_toolbar();
    qk_win_dirty(comp_pid);

    while (1) {
        qk_ipc_recv(&msg);

        if (msg.type == MSG_WIN_RESIZED) {
            uint32_t *ng = (uint32_t *)qk_win_map_buffer(comp_pid, win_id, &gfx_w, &gfx_h);
            if (ng)
                gfx = ng;
            clear_canvas();
            render_toolbar();
            qk_win_dirty(comp_pid);
            continue;
        }

        if (msg.type == MSG_MOUSE_EVENT) {
            Event *e = (Event *)msg.payload;
            int mx = e->x;
            int my = e->y - TITLE_BAR_HEIGHT;

            if (e->type == EVENT_MOUSE_DOWN && e->button == MOUSE_BTN_LEFT) {
                if (my < TOOLBAR_H) {
                    handle_toolbar_click(mx, my);
                } else {
                    drawing = true;
                    if (cur_tool == TOOL_FILL) {
                        uint32_t tgt = gfx[my * gfx_w + mx];
                        flood_fill(mx, my, tgt, draw_color);
                        qk_win_dirty(comp_pid);
                    } else {
                        uint32_t c = (cur_tool == TOOL_ERASER) ? COL_CANVAS : draw_color;
                        draw_dot(mx, my, brush_r[brush_idx], c);
                        qk_win_dirty(comp_pid);
                    }
                    last_x = mx;
                    last_y = my;
                }
            } else if (e->type == EVENT_MOUSE_UP) {
                drawing = false;
                last_x = -1;
                last_y = -1;
            } else if (e->type == EVENT_MOUSE_MOVE && drawing && my >= TOOLBAR_H) {
                if (cur_tool != TOOL_FILL) {
                    uint32_t c = (cur_tool == TOOL_ERASER) ? COL_CANVAS : draw_color;
                    if (last_x >= 0)
                        draw_line(last_x, last_y, mx, my, brush_r[brush_idx], c);
                    else
                        draw_dot(mx, my, brush_r[brush_idx], c);
                    qk_win_dirty(comp_pid);
                }
                last_x = mx;
                last_y = my;
            }
        }
    }
    return 0;
}
