#include "output.h"
#include "string.h"

typedef struct { int x0, y0, x1, y1; } rect_i_t;
static rect_i_t prev_cursor_rect;
static int prev_cursor_valid;
static inline int clamp_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline void copy_row_segment(uint8_t *dst_row, uint8_t *src_row, int start_px, int end_px) {
    if (end_px <= start_px)
        return;
    memcpy(dst_row + start_px * 4, src_row + start_px * 4, (end_px - start_px) * 4);
}
static inline int rect_has_area(rect_i_t r) {
    return (r.x1 > r.x0) && (r.y1 > r.y0);
}
static inline int rect_equals(rect_i_t a, rect_i_t b) {
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}
static inline rect_i_t clamp_cursor_rect(int x, int y, int max_w, int max_h) {
    rect_i_t r;
    r.x0 = clamp_int(x, 0, max_w);
    r.y0 = clamp_int(y, 0, max_h);
    r.x1 = clamp_int(x + CURSOR_W, 0, max_w);
    r.y1 = clamp_int(y + CURSOR_H, 0, max_h);
    return r;
}
static void copy_rect_from_ctx(rect_i_t r, uint8_t *dst, uint8_t *src, int dst_stride,
                               int src_stride) {
    if (!rect_has_area(r))
        return;
    int w_bytes = (r.x1 - r.x0) * 4;
    for (int y = r.y0; y < r.y1; ++y)
        memcpy(dst + y * dst_stride + r.x0 * 4, src + y * src_stride + r.x0 * 4, w_bytes);
}
typedef struct {
    int x0, x1;
} interval_t;
static inline void add_interval_for_row(rect_i_t r, int y, interval_t *out, int *count, int max_w) {
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
void compositor_output_frame(gfx_context_t *frame, fb_info_t *fb, int mx, int my) {
    gfx_context_t fb_ctx = {fb->buffer, fb->width, fb->height, fb->pitch};
    if (!fb->buffer || !frame->buffer)
        return;
    int screen_w = (int)frame->width;
    int screen_h = (int)frame->height;
    if (screen_w <= 0 || screen_h <= 0)
        return;
    rect_i_t new_cursor_rect = clamp_cursor_rect(mx, my, screen_w, screen_h);
    int new_cursor_valid = rect_has_area(new_cursor_rect);
    uint8_t *src = (uint8_t *)frame->buffer;
    uint8_t *dst = (uint8_t *)fb->buffer;
    int src_stride = screen_w * 4;
    int dst_stride = (int)fb->pitch;
    int row_bytes = screen_w * 4;
    if (!prev_cursor_valid && !new_cursor_valid) {
        for (int y = 0; y < screen_h; ++y)
            memcpy(dst + y * dst_stride, src + y * src_stride, row_bytes);
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
