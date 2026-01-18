#include "kernel/syscalls.h"
#include "drivers/GUI/framebuffer.h"
#include "gfx/font_atlas.h"
#include "kernel/process.h"
#include "lib/stdint.h"
#include "util/util.h"
#include "video/window.h"
#include "video/compositor.h"
#include "drivers/timer.h"
#include "fs/fat.h"
#include "mm/kmalloc.h"

extern uint8_t font[];

Window *get_window_by_id(int id) {
    extern Window windows[MAX_WINDOWS]; 
    
    if (id <= 0 || id > MAX_WINDOWS)
        return 0;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].id == id)
            return &windows[i];
    }
    return 0;
}

int sys_window_create(int w, int h, char *title) {
    Window *win = window_create(50, 50, w, h, title);
    if (!win)
        return -1;
    return win->id;
}

void sys_draw_rect(draw_rect_args_t *args) {
    Window *win = get_window_by_id(args->id);
    if (!win) return;

    int buf_w = win->w;
    int buf_h = win->h - TITLE_BAR_HEIGHT;

    int rx = args->x;
    int ry = args->y;
    int rw = args->w;
    int rh = args->h;

    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > buf_w) rw = buf_w - rx;
    if (ry + rh > buf_h) rh = buf_h - ry;

    if (rw <= 0 || rh <= 0) return;

    if (win->gfx_buffer) {
        for (int i = 0; i < rh; i++) {
            int row = ry + i;
            memset32(win->gfx_buffer + (row * buf_w + rx), args->color, rw);
        }
        invalidate_window(win->x, win->y, win->w, win->h);
    }
}

void sys_draw_text(draw_text_args_t *args) {
    Window *win = get_window_by_id(args->id);
    if (!win || !win->gfx_buffer || !main_font)
        return;

    const char *str = args->str;
    int cx = args->x;
    int cy = args->y;

    int buf_w = win->w;
    int buf_h = win->h - TITLE_BAR_HEIGHT;

    while (*str) {
        char c = *str++;
        
        if (c == '\n') {
            cy += main_font->line_height;
            cx = args->x;
            continue;
        }

        if (c < 32 || c > 126) continue;

        GlyphInfo *g = &main_font->glyphs[c - 32];

        int screen_start_y = cy + main_font->ascent + g->off_y;
        int screen_start_x = cx + g->off_x;

        for (int iy = 0; iy < g->h; iy++) {
            for (int ix = 0; ix < g->w; ix++) {
                int win_x = screen_start_x + ix;
                int win_y = screen_start_y + iy;

                if (win_x < 0 || win_x >= buf_w || win_y < 0 || win_y >= buf_h) 
                    continue;

                uint32_t src = main_font->bitmap[(g->y + iy) * main_font->tex_width + (g->x + ix)];
                uint8_t alpha = (src >> 24) & 0xFF;

                if (alpha > 0) {
                    uint32_t *pixel_ptr = &win->gfx_buffer[win_y * buf_w + win_x];
                    *pixel_ptr = blend_colors(args->color, *pixel_ptr, alpha);
                }
            }
        }
        cx += g->advance;
    }
    invalidate_window(win->x, win->y, win->w, win->h);
}

uint32_t sys_get_uptime(){
    return (uint32_t)get_uptime_ms();
}

void sys_sleep(uint32_t ms){
    sleep(ms);
}

int sys_get_key(int win_id) {
    Window *win = get_window_by_id(win_id);
    if (!win) return 0;

    if (win->input_read_ptr == win->input_write_ptr) {
        return 0; 
    }

    char c = win->input_buffer[win->input_read_ptr];
    win->input_read_ptr = (win->input_read_ptr + 1) % INPUT_BUFFER_SIZE;
    return (int)c;
}

void sys_filename_helper(char *out, const char *in) {
    memset(out, ' ', 11);
    int i = 0, j = 0;
    while (in[i] && in[i] != '.' && j < 8) {
        char c = in[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[j++] = c;
    }
    if (in[i] == '.') i++;
    j = 8;
    while (in[i] && j < 11) {
        char c = in[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[j++] = c;
    }
}

int sys_exec(const char *filename) {
    char fat_name[12];
    fat_name[11] = 0;
    sys_filename_helper(fat_name, filename);

    uint32_t size;
    uint8_t *data = fat_read_file(fat_name, &size);
    if (!data) return -1; 

    QuarkExec *qex = (QuarkExec *)data;
    if (qex->magic == 0x5845517F) {
        process_create(data, size);
        kfree(data); 
        return 0; 
    }
    kfree(data);
    return -2;
}

int sys_read_file(const char *filename, char *user_buf) {
    char fat_name[12];
    fat_name[11] = 0;
    sys_filename_helper(fat_name, filename);

    uint32_t size;
    uint8_t *data = fat_read_file(fat_name, &size);
    if (!data) return -1;

    memcpy(user_buf, data, size);
    user_buf[size] = 0;
    
    kfree(data);
    return size;
}

void syscall_handler(struct InterruptRegisters *regs) {
    switch (regs->eax) {
    case 0: 
        for (;;)
            asm volatile("hlt");
        break;
    case 1: 
        break;
    case 2: 
        regs->eax = sys_window_create((int)regs->ebx, (int)regs->ecx, (char *)regs->edx);
        break;
    case 3: 
        break;
    case 4:
        sys_draw_rect((draw_rect_args_t *)regs->ebx);
        break;
    case 5: 
        sys_draw_text((draw_text_args_t *)regs->ebx);
        break;
    case 6:
        regs->eax = sys_get_uptime();
        break;
    case 7:
        sys_sleep((uint32_t)regs->ebx);
        break;
    case 8:
        regs->eax = sys_get_key((int)regs->ebx);
        break;
    case 9:
        regs->eax = sys_exec((const char*)regs->ebx);
        break;
    case 10:
        regs->eax = sys_read_file((const char*)regs->ebx, (char*)regs->ecx);
        break;
    default:
        break;
    }
}