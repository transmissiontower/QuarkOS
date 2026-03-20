#include "user/lib/libapi.h"
#include "user/lib/window.h"
#include "user/lib/storage_client.h"
#include "lib/stdlib.h"
#include "lib/stdbool.h"
#include "kernel/ipc.h"

#define MAX_TABS 8
#define BUF_SIZE 65536
#define MSG_WIN_SET_PIXEL 55
#define MSG_WIN_DRAW_RECT 57
#define MSG_WIN_DRAW_RECT_ROUNDED 58

static int comp_pid = -1;
static int storage_pid = -1;

static int ensure_server_pids(void) {
    if (comp_pid < 0) comp_pid = qk_get_server_pid(QK_SERVER_COMPOSITOR);
    if (storage_pid < 0) storage_pid = qk_get_server_pid(QK_SERVER_STORAGE);
    if (comp_pid < 0 || storage_pid < 0) return -1;
    return 0;
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    message_t msg;
    msg.type = MSG_WIN_DRAW_RECT;
    msg.data1 = y; msg.data2 = x; msg.data3 = w;
    *(int*)&msg.payload[0] = h;
    *(uint32_t*)&msg.payload[4] = color;
    qk_sys_ipc_send(comp_pid, &msg);
}

void draw_rect_rounded(int x, int y, int w, int h, int r, uint32_t color) {
    message_t msg;
    msg.type = MSG_WIN_DRAW_RECT_ROUNDED;
    msg.data1 = y; msg.data2 = x; msg.data3 = w;
    *(int*)&msg.payload[0] = h;
    *(int*)&msg.payload[4] = r;
    *(uint32_t*)&msg.payload[8] = color;
    qk_sys_ipc_send(comp_pid, &msg);
}

void strcat_custom(char* dest, const char* src) {
    int d_len = strlen(dest);
    int i = 0;
    while (src[i]) {
        dest[d_len + i] = src[i];
        i++;
    }
    dest[d_len + i] = '\0';
}

void strncat_custom(char* dest, const char* src, int n) {
    int d_len = strlen(dest);
    int i = 0;
    while (src[i] && i < n) {
        dest[d_len + i] = src[i];
        i++;
    }
    dest[d_len + i] = '\0';
}

char notepad_buffer[BUF_SIZE];
int notepad_cursor = 0;
int notepad_scroll_y = 0;
char notepad_filename[64] = "Welcome.txt";

char scancode_to_char_v2(uint8_t code, bool shift) {
    if (code == 0x39) return ' ';
    if (code == 0x1C) return '\n';
    if (code == 0x0E) return '\b';

    const char* row1 = "1234567890-=";
    const char* row1_s = "!@#$%^&*()_+";
    if (code >= 0x02 && code <= 0x0D) return shift ? row1_s[code-0x02] : row1[code-0x02];

    const char* row2 = "qwertyuiop[]";
    const char* row2_s = "QWERTYUIOP{}";
    if (code >= 0x10 && code <= 0x1B) return shift ? row2_s[code-0x10] : row2[code-0x10];

    const char* row3 = "asdfghjkl;'`";
    const char* row3_s = "ASDFGHJKL:\"~";
    if (code >= 0x1E && code <= 0x29) return shift ? row3_s[code-0x1E] : row3[code-0x1E];

    const char* row4 = "zxcvbnm,./";
    const char* row4_s = "ZXCVBNM<>?";
    if (code >= 0x2C && code <= 0x35) return shift ? row4_s[code-0x2C] : row4[code-0x2C];

    return 0;
}

void int_to_str_padded(int n, char* buf, int width) {
    char tmp[16];
    int_to_str(n, tmp, 0);
    int len = strlen(tmp);
    int pos = 0;
    while (pos < width - len) buf[pos++] = ' ';
    strncpy(buf + pos, tmp, 15);
    buf[pos + len] = 0;
}

void refresh_display() {
    message_t msg;
    msg.type = MSG_WIN_CLEAR;
    qk_sys_ipc_send(comp_pid, &msg);

    
    draw_rect(0, 0, 800, 600, COLOR_WIN_BG);

    
    char line_info[32];
    int line_num = 1;
    char* ptr = notepad_buffer;
    int skipped = 0;
    while (*ptr && skipped < notepad_scroll_y) {
        if (*ptr == '\n') skipped++;
        ptr++;
    }
    line_num += skipped;

    int lines_rendered = 0;
    while (*ptr && lines_rendered < 29) { 
        if (ptr == notepad_buffer || *(ptr-1) == '\n') {
            int_to_str_padded(line_num++, line_info, 3);
            strcat_custom(line_info, " "); 
            msg.type = MSG_WIN_PRINT;
            strncpy((char*)msg.payload, line_info, 127);
            qk_sys_ipc_send(comp_pid, &msg);
        }

        char tmp[2] = {*ptr, 0};
        msg.type = MSG_WIN_PRINT;
        strncpy((char*)msg.payload, tmp, 127);
        qk_sys_ipc_send(comp_pid, &msg);

        if (*ptr == '\n') lines_rendered++;
        ptr++;
    }
}

void save_current_tab() {
    uint32_t len = strlen(notepad_buffer);
    storage_send_file(storage_pid, 0, notepad_filename, (const uint8_t*)notepad_buffer, len);
}

void handle_mouse_click(int x, int y) {
    
    
    
    
    
    
    if (y >= 0 && y < 32) {
        if (x >= 0 && x <= 60) {
            
            save_current_tab();
        } else if (x >= 60 && x <= 120) {
            
            notepad_cursor = 0;
            notepad_buffer[0] = 0;
            notepad_scroll_y = 0;
            strncpy(notepad_filename, "Untitled.txt", 63);
            refresh_display();
        } else if (x >= 120 && x <= 180) {
            
            notepad_cursor = 0;
            notepad_buffer[0] = 0;
            notepad_scroll_y = 0;
            refresh_display();
        }
    }
}

int main() {
    if (ensure_server_pids() != 0) return 1;
    memset(notepad_buffer, 0, BUF_SIZE);
    
    const char* init_text = "# QuarkOS Graphics Mode\n# Ctrl+S: Save\n";
    strncpy(notepad_buffer, init_text, BUF_SIZE - 1);
    notepad_cursor = strlen(notepad_buffer);

    message_t msg;
    msg.type = MSG_WIN_CREATE;
    msg.data1 = 800;
    msg.data2 = 600;
    strncpy((char*)msg.payload, " Save   New   Clean ", 21);
    qk_sys_ipc_send(comp_pid, &msg);

    while(1) {
        qk_ipc_recv(&msg);
        if (msg.type == MSG_WIN_CREATED) break;
    }

    refresh_display();

    bool lshift = false, rshift = false;
    bool lctrl = false;

    while(1) {
        qk_ipc_recv(&msg);
        if (msg.type == MSG_KEY_EVENT) {
            uint8_t scancode = (uint8_t)msg.data1;
            bool released = scancode & 0x80;
            scancode &= 0x7F;

            if (scancode == 0x2A) lshift = !released;
            if (scancode == 0x36) rshift = !released;
            if (scancode == 0x1D) lctrl = !released;

            if (released) continue;

            if (lctrl && scancode == 0x1F) { 
                save_current_tab();
                continue;
            }

            if (scancode == 0x49) { 
                if (notepad_scroll_y > 0) {
                    notepad_scroll_y--;
                    refresh_display();
                }
                continue;
            }
            if (scancode == 0x51) { 
                notepad_scroll_y++;
                refresh_display();
                continue;
            }

            char c = scancode_to_char_v2(scancode, lshift || rshift);
            if (c != 0) {
                if (c == '\b') {
                    if (notepad_cursor > 0) {
                        notepad_cursor--;
                        notepad_buffer[notepad_cursor] = 0;
                    }
                } else {
                    if (notepad_cursor < BUF_SIZE - 1) {
                        notepad_buffer[notepad_cursor++] = c;
                        notepad_buffer[notepad_cursor] = 0;
                    }
                }
                refresh_display();
            }
        } else if (msg.type == MSG_MOUSE_EVENT) {
            if (msg.data1 == EVENT_MOUSE_DOWN) {
                handle_mouse_click((int)msg.data2, (int)msg.data3);
            }
        }
    }
    return 0;
}
