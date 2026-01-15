#ifndef KMALLOC_H
#define KMALLOC_H

#include "lib/stdint.h"
#include "lib/stdbool.h"

typedef struct Header {
    struct Header *next;
    uint32_t size;
    bool is_free;
    uint32_t magic;
} Header;

#define HEAP_START KERNEL_MALLOC
#define HEADER_SIZE sizeof(Header)
#define MAGIC_NUM 0x12345678

void kmalloc_init(uint32_t initialHeapSize);
void change_heap_size(int newBytes);
void *kmalloc(uint32_t size);
void kfree(void *ptr);

#endif