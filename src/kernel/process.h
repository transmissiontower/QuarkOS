#ifndef PROCESS_H
#define PROCESS_H

#include "lib/stdint.h"

typedef struct process
{
    int pid;
    uint32_t esp;
    uint32_t kernel_stack;
    uint32_t *page_dir;
    struct process *next;
} process_t;

typedef struct {
    uint32_t magic;      // 0x5845517F
    uint32_t entry;
    uint32_t code_size;
    uint32_t data_size;
    uint32_t bss_size;
} __attribute__((packed)) QuarkExec;

extern process_t *current_process;

void scheduler_init();
void process_create(uint8_t *code, uint32_t size);
void schedule();

#endif
