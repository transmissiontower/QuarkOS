#include "kernel/shell.h"
#include "lib/stdlib.h"
#include "util/util.h"
#include "video/window.h"
#include "fs/fat.h"
#include "kernel/process.h"
#include "mm/kmalloc.h"

void filename_to_fat(char *out, char *in) {
    memset(out, ' ', 11);
    int i = 0;
    int j = 0;

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

int str_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*prefix++ != *str++) return 0;
    }
    return 1;
}

void command_run(Window *win, char *cmd) {
    if (cmd[0] == 0) return;

    if (strcmp(cmd, "help") == 0) {
        window_print(win, "\nCommands:\n - help\n - clear\n - cat <file>\n - exec <file>\n - shutdown\n");
    }
    else if (strcmp(cmd, "clear") == 0) {
        window_clear(win);
        return;
    }
    else if (strcmp(cmd, "shutdown") == 0) {
        // TODO
    }

    else if (str_starts_with(cmd, "cat ")) {
        char *filename = cmd + 4; 
        char fat_name[12]; 
        fat_name[11] = 0;
        
        filename_to_fat(fat_name, filename);
        
        uint32_t size;
        uint8_t *data = fat_read_file(fat_name, &size);
        
        if (data) {
            char *text = (char*)kmalloc(size + 1);
            memcpy(text, data, size);
            text[size] = 0;
            
            window_print(win, "\n");
            window_print(win, text);
            window_print(win, "\n");
            
            kfree(text);
            kfree(data);
        } else {
            window_print(win, "\nFile not found: ");
            window_print(win, fat_name);
            window_print(win, "\n");
        }
    }
else if (str_starts_with(cmd, "exec ")) {
        char *filename = cmd + 5; 
        char fat_name[12];
        fat_name[11] = 0;
        
        filename_to_fat(fat_name, filename);
        
        uint32_t size;
        uint8_t *data = fat_read_file(fat_name, &size);
        
        if (data) {
            QuarkExec *qex = (QuarkExec *)data;
            if (qex->magic == 0x5845517F) {
                window_print(win, "\nLaunching process...\n");
                process_create(data, size);
            } else {
                window_print(win, "\nInvalid QEX executable.\n");
            }

            kfree(data); 
        } else {
            window_print(win, "\nFile not found.\n");
        }
    }
    else {
        window_print(win, "\nUnknown command.\n");
    }
}