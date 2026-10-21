#ifndef DESKTOP_SHELL_H
#define DESKTOP_SHELL_H

#include "user/libui/gfx.h"

typedef struct { int x, y, w, h; } ui_rect_t;
extern ui_rect_t power_button_rect;

void desktop_shell_render_bar(gfx_context_t *ctx, int current_fps);
void desktop_shell_dim(gfx_context_t *ctx, uint32_t color, uint8_t alpha);
void compositor_notify(const char *text);

#endif
