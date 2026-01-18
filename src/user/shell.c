#include "user/lib/libapi.h"

int my_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int str_starts_with(const char *str, const char *prefix) {
    while (*prefix) { if (*prefix++ != *str++) return 0; }
    return 1;
}

#define LINE_HEIGHT 20
#define BG_COLOR    0xFF2E2E2E

int cursor_y = 5;
int win_id = 0;

void shell_print(const char* text) {
    if (cursor_y >= 180) {
        qk_draw_rect(win_id, 0, 0, 300, 200, BG_COLOR);
        cursor_y = 5;
    }
    qk_draw_text(win_id, 5, cursor_y, text, 0xFFFFFFFF);
    
    int i = 0;
    while(text[i]) {
        if(text[i] == '\n') cursor_y += LINE_HEIGHT;
        i++;
    }
    cursor_y += LINE_HEIGHT; 
}

int main() {
    win_id = qk_window_create(300, 200, "Shell");
    if(win_id < 0) return 1;
    
    char buffer[64];
    char file_buffer[1024];

    qk_draw_rect(win_id, 0, 0, 300, 200, BG_COLOR);
    shell_print("QuarkOS User Shell");

    while(1) {
        qk_draw_text(win_id, 5, cursor_y, "> ", 0xFF00FF00);
        
        int i = 0;
        for(int k=0; k<64; k++) buffer[k] = 0;

        while(i < 63) {
            int key = qk_get_key(win_id);
            if(key == 0) {
                qk_sleep(10);
                continue;
            }
            
            char c = (char)key;
            if(c == '\n') break; 
            
            if(c == '\b') {
                if(i > 0) {
                    i--;
                    buffer[i] = 0;
                }
            } else {
                buffer[i++] = c;
            }

            qk_draw_rect(win_id, 20, cursor_y, 280, LINE_HEIGHT, BG_COLOR);
            qk_draw_text(win_id, 20, cursor_y, buffer, 0xFFFFFFFF);
        }
        
        buffer[i] = 0;
        cursor_y += LINE_HEIGHT;

        if (my_strcmp(buffer, "help") == 0) {
            shell_print("Commands:\n - help\n - clear\n - cat <file>\n - exec <file>");
        }
        else if (my_strcmp(buffer, "clear") == 0) {
            qk_draw_rect(win_id, 0, 0, 300, 200, BG_COLOR);
            cursor_y = 5;
        }
        else if (str_starts_with(buffer, "cat ")) {
            char *filename = buffer + 4;
            int bytes = qk_read_file(filename, file_buffer);
            if (bytes > 0) shell_print(file_buffer);
            else shell_print("File not found or empty.");
        }
        else if (str_starts_with(buffer, "exec ")) {
            char *filename = buffer + 5;
            shell_print("Launching...");
            int ret = qk_exec(filename);
            if (ret != 0) shell_print("Failed to launch.");
        }
        else if (i > 0) {
            shell_print("Unknown command.");
        }
        
        qk_sleep(20);
    }
    return 0;
}