#include "user/libui/gfx.h"
#include "user/servers/compositor/desktop_shell.h"
#include "user/libui/icons_def.h"
#include "user/libui/window.h"
#include "user/libui/font_atlas.h"
#include "user/libc/libapi.h"
#include "stdlib.h"
#include "string.h"

void desktop_shell_dim(gfx_context_t *ctx, uint32_t color, uint8_t alpha) {
    if (!alpha)
        return;
    uint32_t pixels = ctx->width * ctx->height;
    for (uint32_t i = 0; i < pixels; ++i)
        ctx->buffer[i] = gfx_blend_colors(color, ctx->buffer[i], alpha);
}

static void draw_pill(gfx_context_t *ctx, int x, int y, int w, int h) {
    gfx_draw_shadow(ctx, x, y, w, h, 7, 12);
    gfx_rect_rounded_fill(ctx, x, y, w, h, 7, COLOR_WIN_HEADER);
}
void desktop_shell_render_bar(gfx_context_t *ctx, int current_fps) {
    int y = 10;
    int h = 35;
    uint32_t fg = COLOR_WIN_ACTIVE;
    int search_x = 10;
    int search_w = 35;
    draw_pill(ctx, search_x, y, search_w, h);
    draw_icon_sized(ctx, gui_icons, search_x + (search_w - 18) / 2 - 1, y + (h - 18) / 2,
                    ICON_SEARCH, fg, 18);
    int clock_w = 150;
    int clock_x = ((int)ctx->width - clock_w) / 2;
    int clock_y = y + 22;
    draw_pill(ctx, clock_x, y, clock_w, h);
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
    draw_string_vector(ctx, clock_x + 20, clock_y, date_str, fg);
    draw_string_vector(ctx, clock_x + clock_w - 20 - get_string_width(time_str), clock_y, time_str,
                       fg);
    int buttons_x = (int)ctx->width - 58;
    int buttons_w = 48;
    int buttons_y = 19;
    draw_pill(ctx, buttons_x, y, buttons_w, h);
    draw_icon_sized(ctx, gui_icons, buttons_x + 15, buttons_y, ICON_SHUTDOWN, fg, 18);
    power_button_rect.x = buttons_x + 16 - 4;
    power_button_rect.y = buttons_y - 4;
    power_button_rect.w = 28;
    power_button_rect.h = 28;
    int fps_x = buttons_x - 110;
    int fps_w = 100;
    int fps_y = y + 22;
    draw_pill(ctx, fps_x, y, fps_w, h);
    char fps_buf[16];
    int_to_str(current_fps, fps_buf, 10);
    draw_string_vector(ctx, fps_x + 20, fps_y, "CPU", fg);
    draw_string_vector(ctx, fps_x + 60, fps_y, fps_buf, fg);
}
