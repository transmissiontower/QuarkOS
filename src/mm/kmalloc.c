#include "mm/kmalloc.h"
#include "lib/stdint.h"
#include "mm/memory.h"
#include "util/util.h"

static Header *head = 0;
static uint32_t heap_end_virt = HEAP_START;

void kmalloc_init(uint32_t initialHeapSize) {
    heap_end_virt = HEAP_START;
    head = 0;
    change_heap_size(initialHeapSize);
}

void change_heap_size(int new_bytes) {
    if (new_bytes % 4096 != 0) {
        new_bytes = (new_bytes & ~0xFFF) + 0x1000;
    }

    uint32_t old_end = heap_end_virt;
    uint32_t pages = new_bytes / 4096;

    for (uint32_t i = 0; i < pages; i++) {
        uint32_t phys = pmm_alloc_page_frame();
        if (!phys) {
            kernel_panic("OOM in change_heap_size");
            return;
        }
        mem_map_page(old_end + i * 4096, phys, PAGE_FLAG_WRITE | PAGE_FLAG_PRESENT);
    }

    heap_end_virt += new_bytes;

    Header *new_block = (Header *)old_end;
    new_block->size = new_bytes - HEADER_SIZE;
    new_block->is_free = true;
    new_block->next = 0;
    new_block->magic = MAGIC_NUM;

    if (head == 0) {
        head = new_block;
    } else {
        Header *current = head;
        while (current->next != 0) {
            current = current->next;
        }
        current->next = new_block;
    }
}

void *kmalloc(uint32_t size) {
    if (size == 0)
        return 0;

    uint32_t aligned_size = size = (size + 3) & ~3;

    Header *current = head;
    while (current != 0) {
        if (current->is_free && current->size >= aligned_size) {
            if (current->size > aligned_size + HEADER_SIZE + 16) {
                Header *next_block = (Header *)((uint32_t)current + HEADER_SIZE + aligned_size);
                next_block->size = current->size - aligned_size - HEADER_SIZE;
                next_block->is_free = true;
                next_block->next = current->next;
                next_block->magic = MAGIC_NUM;

                current->size = aligned_size;
                current->next = next_block;
            }
            current->is_free = false;
            return (void *)((uint32_t)current + HEADER_SIZE);
        }
        current = current->next;
    }

    uint32_t bytes_needed = aligned_size + HEADER_SIZE;
    uint32_t chunk_size = 0x100000;
    if (bytes_needed > chunk_size)
        chunk_size = bytes_needed;

    change_heap_size(chunk_size);

    return kmalloc(size);
}

void kfree(void *ptr) {
    if (!ptr)
        return;

    Header *block = (Header *)((uint32_t)ptr - HEADER_SIZE);

    if (block->magic != MAGIC_NUM) {
        return;
    }
    block->is_free = true;

    Header *current = head;
    while (current != 0 && current->next != 0) {
        if (current->is_free && current->next->is_free) {
            if ((uint32_t)current + HEADER_SIZE + current->size == (uint32_t)current->next) {
                current->size += HEADER_SIZE + current->next->size;
                current->next = current->next->next;
            } else {
                current = current->next;
            }
        } else {
            current = current->next;
        }
    }
}