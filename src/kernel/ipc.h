#ifndef IPC_H
#define IPC_H

#include "lib/stdint.h"

typedef struct {
    int sender_pid;
    int type;
    uint32_t data1, data2, data3;
    uint8_t payload[512];
} message_t;

#define MSG_OK 100
#define MSG_ERROR 101
#define MSG_WIN_CREATE 50
#define MSG_WIN_PRINT 51
#define MSG_WIN_CLEAR 52
#define MSG_WIN_CREATED 53
#define MSG_KEY_EVENT 200
#define MSG_MOUSE_EVENT 201

#define MSG_FILE_OPEN 300
#define MSG_FILE_SIZE 301
#define MSG_REQ_DATA 302
#define MSG_FILE_DATA 303
#define MSG_FILE_SAVE 304
#define MSG_DIR_REQ 305
#define MSG_DIR_REPLY 306
#define MSG_DIR_CREATE 307
#define MSG_FILE_SAVE_BEGIN 308
#define MSG_FILE_SAVE_CHUNK 309
#define MSG_FILE_SAVE_END 310
#define MSG_FILE_READ 311
#define MSG_FILE_PEEK 312

#define MSG_WIN_SET_PIXEL 55
#define MSG_WIN_DRAW_LINE 56
#define MSG_WIN_DRAW_RECT 57
#define MSG_WIN_DRAW_RECT_ROUNDED 58
#define MSG_WIN_PRINT_AT 59
#define MSG_WIN_COLOR 60
#define MSG_PROCESS_TERMINATED 61
#define MSG_WIN_BLIT_PIXELS 62
#define MSG_WIN_MAP_BUFFER  63  
#define MSG_WIN_BUFFER_INFO 64  
#define MSG_WIN_DIRTY       65  
#define MSG_COMPOSITOR_SUSPEND 70
#define MSG_COMPOSITOR_RESUME  71
#define MSG_INPUT_REDIRECT     72  
#define MSG_INPUT_RESTORE      73  

#define IPC_QUEUE_SIZE 128

typedef struct {
    message_t messages[IPC_QUEUE_SIZE];
    int head, tail, count;
} message_queue_t;

#define IO_PRIV_ATA 0x01
#define IO_PRIV_PS2 0x02

#endif
