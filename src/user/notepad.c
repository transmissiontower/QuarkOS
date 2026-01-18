#include "user/lib/libapi.h"

#define BG_COLOR    0xFF2E2E2E
#define TEXT_COLOR  0xFFFFFFFF
#define BUFFER_SIZE 2048

char doc_buffer[BUFFER_SIZE];
int doc_len = 0;

int main() {
    int win_id = qk_window_create(400, 300, "Notepad");
    if(win_id < 0) return 1;

    for(int i=0; i<BUFFER_SIZE; i++) doc_buffer[i] = 0;
    doc_len = 0;

    qk_draw_rect(win_id, 0, 0, 400, 300, BG_COLOR);
    
    int blink_timer = 0;
    int show_cursor = 1;

    while(1) {
        int key = qk_get_key(win_id);
        int need_redraw = 0;

        if (key > 0) {
            char c = (char)key;
            if (c == '\b') {
                if (doc_len > 0) {
                    doc_len--;
                    doc_buffer[doc_len] = 0;
                    need_redraw = 1;
                }
            } 
            else if (doc_len < BUFFER_SIZE - 2) {
                if (c == '\r') c = '\n';
                doc_buffer[doc_len++] = c;
                doc_buffer[doc_len] = 0;
                need_redraw = 1;
            }
        }

        blink_timer++;
        if (blink_timer > 20) { 
            show_cursor = !show_cursor;
            blink_timer = 0;
            need_redraw = 1; 
        }

        if (need_redraw) {
            qk_draw_rect(win_id, 0, 0, 400, 300, BG_COLOR);
            qk_draw_text(win_id, 10, 10, doc_buffer, TEXT_COLOR);

            if (show_cursor) {
                doc_buffer[doc_len] = '_';
                doc_buffer[doc_len+1] = 0;
                qk_draw_text(win_id, 10, 10, doc_buffer, TEXT_COLOR);
                doc_buffer[doc_len] = 0; 
            }
        }
        qk_sleep(20);
    }
    return 0;
}