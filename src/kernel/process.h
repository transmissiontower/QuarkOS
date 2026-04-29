// SPDX-License-Identifier: MIT
/*
 * src/kernel/process.h - Process control block, states, and scheduler interface
 */
#ifndef PROCESS_H
#define PROCESS_H
#include "kernel/ipc.h"
#include "kernel/spinlock.h"
#include "lib/stdint.h"
typedef enum {
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_ZOMBIE,
    PROCESS_WAIT_IRQ,
    PROCESS_IPC_WAIT,
    PROCESS_WAIT_PID
} process_state_t;
typedef struct process {
    int pid;
#if defined(__aarch64__)

    uintptr_t esp;
    uintptr_t kernel_stack;
    uintptr_t kstack_alloc;
    uintptr_t pd_phys;
#else
    uint32_t esp;
    uint32_t kernel_stack;
    uint32_t kstack_alloc;
    uint32_t pd_phys;
#endif
    uint32_t heap_end;
    uint32_t image_base;
    uint32_t image_end;
    struct process *next;
    process_state_t state;
    uint64_t wake_up_time;
    char name[16];
    message_queue_t ipc_queue;
    spinlock_t ipc_lock;
    uint32_t io_privilege_mask;
    int waiting_irq;
    int ipc_wait_from;
    int ipc_reply_ready;
    message_t ipc_reply_slot;
    uint32_t shared_virt_bump;
    int exit_code;
    int waitpid_target;
    char env[256];
} process_t;
typedef struct {
    uint32_t magic;
    uint32_t entry;
    uint32_t code_size;
    uint32_t data_size;
    uint32_t bss_size;
} __attribute__((packed)) QuarkExec;
typedef struct {
    int pid;
    process_state_t state;
    char name[16];
} process_info_t;
extern process_t * volatile current_process;
void k_scheduler_init(void);
void k_scheduler_update_sleeps(void);
process_t *k_proc_spawn(uint8_t *code, uint32_t size);
void k_scheduler_yield(void);
void k_scheduler_direct_switch(struct process *dest);
void block_current_process(uint32_t ms);
process_t *k_proc_find_by_id(int pid);
void k_proc_wait_irq(int irq);
void process_wake_on_irq(int irq);
void k_proc_terminate(int pid);
process_t *get_current_process(void);

#if defined(__aarch64__)
long k_syscall_dispatch_aarch64(long num, long arg1, long arg2, long arg3);
#endif

#endif
