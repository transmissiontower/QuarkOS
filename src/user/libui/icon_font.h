#ifndef ICON_FONT_H
#define ICON_FONT_H
#include "stdint.h"
#include "user/libui/gfx.h"
int icon_font_init(unsigned char *ttf_data);
void draw_icon_font_scaled_centered(gfx_context_t *ctx, int icon_id, int cx, int cy, float scale,
                                    uint32_t color);
#endif
