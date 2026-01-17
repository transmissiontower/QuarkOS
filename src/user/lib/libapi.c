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