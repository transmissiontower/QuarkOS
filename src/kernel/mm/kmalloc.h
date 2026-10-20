#ifndef KMALLOC_H
#define KMALLOC_H
#include "stdbool.h"
#include "stdint.h"
typedef struct Header {
    struct Header *next;
    uint32_t size;
    bool is_free;
    uint32_t magic;
} Header;
#define HEAP_START 0xD0000000
#define HEADER_SIZE sizeof(Header)
#define MAGIC_NUM 0x12345678
void mm_heap_init(uint32_t initialHeapSize);
void mm_heap_expand(int newBytes);
void *mm_heap_alloc(uint32_t size);
void mm_heap_free(void *ptr);
#endif
