#include "libapi.h"

void qk_exit(int code){
    asm volatile("int $0x80" : : "a"(0), "b"(code));
    while(1);
}

int qk_window_create(int w, int h, const char* title){
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(2), "b"(w), "c"(h), "d"(title));
    return ret;
}

void qk_draw_rect(int id, int x, int y, int w, int h, uint32_t color){
    draw_rect_args_t args = {id, x, y, w, h, color};
    asm volatile("int $0x80" : : "a"(4), "b"(&args) : "memory");
}

void qk_draw_text(int id, int x, int y, const char* str, uint32_t color){
    draw_text_args_t args = {id, x, y, str, color};
    asm volatile("int $0x80" : : "a"(5), "b"(&args) : "memory");
}

uint32_t qk_get_uptime(){
    uint32_t ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(6));
    return ret;
}

void qk_sleep(uint32_t ms){
    asm volatile("int $0x80" : : "a"(7), "b"(ms));
}

int qk_get_key(int win_id) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(8), "b"(win_id));
    return ret;
}

int qk_exec(const char *filename) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(9), "b"(filename) : "memory");
    return ret;
}

int qk_read_file(const char *filename, char *buffer) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(10), "b"(filename), "c"(buffer) : "memory");
    return ret;
}

void qk_scan(int win_id, char *buffer, int max_len) {
    int i = 0;
    char temp[2] = {0, 0}; 
    
    while (i < max_len - 1) {
        int key = qk_get_key(win_id);
        if (key == 0) {
            qk_sleep(10); 
            continue;
        }
        
        char c = (char)key;
        
        if (c == '\n') {
            break;
        } else if (c == '\b') {
            if (i > 0) {
                i--;
            }
        } else {
            buffer[i++] = c;
        }
    }
    buffer[i] = 0;
}
