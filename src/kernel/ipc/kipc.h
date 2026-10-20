#ifndef KERNEL_KIPC_H
#define KERNEL_KIPC_H
#include "stdint.h"
#include "abi/ipc.h"
#define IPC_QUEUE_SIZE 128
typedef struct {
    message_t messages[IPC_QUEUE_SIZE];
    int head, tail, count;
} message_queue_t;
#endif
