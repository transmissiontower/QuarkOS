#ifndef SODIUM_COMMAND_H
#define SODIUM_COMMAND_H

#include "abi/ipc.h"
#include "stdbool.h"
#include "stdlib.h"
#include "user/libc/libapi.h"
#include "user/libc/path.h"
#include "user/clients/storage_client.h"
#include <stddef.h>

#define C_RESET "\033[0m"
#define C_DIM "\033[2m"
#define C_RED "\033[31m"
#define C_GREEN "\033[32m"
#define C_YELLOW "\033[33m"
#define C_CYAN "\033[36m"
#define C_BCYAN "\033[96m"
#define NANO_BUFFER_SIZE 4096
#define SCANCODE_ARROW_UP 0x48
#define SCANCODE_ARROW_DOWN 0x50
#define SCANCODE_CTRL 0x1D
#define SPECIAL_CTRL_C 0x03
#define KEY_MOD_SHIFT 0x01
#define KEY_MOD_CAPS 0x08

extern int storage_pid;
extern uint32_t current_dir_cluster;
extern int current_disk;

void tty_print(const char *text);
void tty_clear(void);
int my_strcmp(const char *a, const char *b);
int resolve_target(const char *input, uint32_t *out_dir, char *out_name);
void *read_file(const char *filename, uint32_t *out_size);
int save_file(const char *path, const char *data, uint32_t size);
int read_line_blocking(char *out, int max_len, uint8_t *special_sc);

void command_main(const char *args);

#endif
