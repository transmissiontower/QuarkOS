#ifndef COMPOSITOR_PRESENT_H
#define COMPOSITOR_PRESENT_H

#include "drivers/video/bitmaps.h"
#include "user/libc/libapi.h"
#include "user/libui/gfx.h"

void compositor_output_frame(gfx_context_t *frame, fb_info_t *fb, int cursor_x, int cursor_y);

#endif
