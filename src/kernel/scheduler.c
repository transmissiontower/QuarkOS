// SPDX-License-Identifier: MIT
/*
 * src/kernel/scheduler.c - Round-robin preemptive process scheduler
 */
#if defined(__aarch64__)
#include "arch/aarch64/platform.h"
void arch_tss_set_stack(uintptr_t kernel_stack);
#elif defined(__arm__)
#include "arch/arm/platform.h"
void arch_tss_set_stack(uint32_t kernel_stack);
#else
#include "arch/x86/gdt.h"
#endif
#include "drivers/GUI/framebuffer.h"
#include "kernel/elf.h"
#include "kernel/klog.h"
#include "kernel/process.h"
#include "lib/stdlib.h"
#include "mm/kmalloc.h"
#include "mm/memory.h"
#include "util/util.h"
#if defined(__aarch64__)
#define KSTACK_SIZE 32768
#define KSTACK_GUARD 4096
#else
#define KSTACK_SIZE 4096
#define KSTACK_GUARD 0
#endif
process_t *volatile current_process = 0;
process_t *ready_queue = 0;
static int next_pid = 1;
#if defined(__aarch64__)

__attribute__((returns_twice)) extern void switch_context(uintptr_t *old_esp, uintptr_t new_esp);
#else
extern void switch_context(uint32_t *old_esp, uint32_t new_esp);
#endif
extern void irq_ret_stub();
extern uint64_t get_uptime_ms();
void mm_pmm_free_frame(uint32_t phys);
void mm_vmm_cleanup_current_dir();
static process_t *process_to_free = 0;

#if defined(__aarch64__)

void validate_current(const char *tag) {
    uintptr_t cp = (uintptr_t)current_process;

    if (cp < 0xD0000000UL || cp >= 0xE0000000UL) {
        KLOG_PROC(LOG_CRIT, "CORRUPT current_process=%lx at %s", (uint64_t)cp, tag);
        extern void kernel_panic(const char *msg);
        kernel_panic("current_process out of range");
    }
    int pid = current_process->pid;
    if (pid < 0 || pid > 64) {
        KLOG_PROC(LOG_CRIT, "CORRUPT pid=%d cp=%lx at %s", pid, (uint64_t)cp, tag);
        extern void kernel_panic(const char *msg);
        kernel_panic("current_process->pid out of range");
    }

    uint64_t sp_val;
    asm volatile("mov %0, sp" : "=r"(sp_val));
    if (pid == 0) {
        extern uint32_t stack_top;
        uintptr_t stop = (uintptr_t)&stack_top;
        if (sp_val > stop || sp_val < stop - 131072) {
            KLOG_PROC(LOG_CRIT, "SP_CORRUPT pid=0 sp=%lx stack_top=%lx at %s", sp_val,
                      (uint64_t)stop, tag);
            extern void kernel_panic(const char *msg);
            kernel_panic("kernel SP out of range");
        }
    } else {
        uintptr_t lo = current_process->kstack_alloc;
        uintptr_t hi = current_process->kernel_stack + 4096;
        if (lo && (sp_val < lo || sp_val > hi)) {
            KLOG_PROC(LOG_CRIT, "SP_CORRUPT pid=%d sp=%lx range=%lx-%lx at %s", pid, sp_val,
                      (uint64_t)lo, (uint64_t)hi, tag);
            extern void kernel_panic(const char *msg);
            kernel_panic("process SP out of kernel stack range");
        }
    }
}
#else
#define validate_current(tag) ((void)0)
#endif

typedef enum { EXEC_FMT_NONE = 0, EXEC_FMT_QEX = 1, EXEC_FMT_ELF = 2 } exec_format_t;
static int load_quark_exec_image(const QuarkExec *exec, uint32_t size, exec_image_info_t *info) {
    if (!exec || !info || size <= sizeof(QuarkExec))
        return -1;
    uint32_t user_base = 0x400000;
    uint32_t payload_size = size - sizeof(QuarkExec);
    uint32_t total_mem_size = payload_size + exec->bss_size;
    if (total_mem_size == 0)
        return -1;
    uint32_t pages_needed = (total_mem_size + 4095) / 4096;
    if (pages_needed == 0)
        pages_needed = 1;
    uint32_t flags = PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE | PAGE_FLAG_USER;
    const uint8_t *payload = ((const uint8_t *)exec) + sizeof(QuarkExec);
    uint32_t payload_off = 0;
    for (uint32_t i = 0; i < pages_needed; i++) {
        uint32_t phys = mm_pmm_alloc_frame();
        if (!phys)
            return -1;
        mm_vmm_map_page(user_base + (i * 4096), phys, flags);
#if defined(__arm__) || defined(__aarch64__)

        uint8_t *page_virt = (uint8_t *)(uintptr_t)(phys + 0x80000000U);
        uint32_t page_start = i * 4096;
        uint32_t page_end = page_start + 4096;

        if (page_start < payload_size) {
            uint32_t copy_start = page_start;
            uint32_t copy_end = payload_size < page_end ? payload_size : page_end;
            memcpy(page_virt + (copy_start - page_start), payload + copy_start,
                   copy_end - copy_start);

            if (copy_end < page_end) {
                uint32_t zero_start = copy_end - page_start;
                memset(page_virt + zero_start, 0, page_end - page_start - zero_start);
            }
        } else {
            memset(page_virt, 0, 4096);
        }

        for (uint32_t off = 0; off < 4096; off += 64)
            asm volatile("dc cvau, %0" ::"r"((uint64_t)(uintptr_t)(page_virt + off)) : "memory");
#endif
    }
#if !defined(__arm__) && !defined(__aarch64__)
    memcpy((void *)user_base, payload, payload_size);
    if (exec->bss_size)
        memset((void *)(user_base + payload_size), 0, exec->bss_size);
#endif
    info->entry = exec->entry;
    info->image_base = user_base;
    info->image_end = user_base + total_mem_size;
    if (info->entry < info->image_base || info->entry >= info->image_end)
        return -1;
    return 0;
}
void k_scheduler_init() {
    process_t *kernel_proc = (process_t *)mm_heap_alloc(sizeof(process_t));
    kernel_proc->pid = 0;
#if defined(__aarch64__)
    kernel_proc->pd_phys = (uintptr_t)arm_get_ttbr0();
#elif defined(__arm__)
    kernel_proc->pd_phys = arm_get_ttbr0();
#else
    uint32_t pd_phys;
    asm volatile("mov %%cr3, %0" : "=r"(pd_phys));
    kernel_proc->pd_phys = pd_phys;
#endif
    kernel_proc->state = PROCESS_RUNNING;
    kernel_proc->next = kernel_proc;
    kernel_proc->io_privilege_mask = 0xFFFFFFFF;
    kernel_proc->ipc_queue.head = 0;
    kernel_proc->ipc_queue.tail = 0;
    kernel_proc->ipc_queue.count = 0;
    kernel_proc->ipc_lock = (spinlock_t)SPINLOCK_INIT;
    kernel_proc->wake_up_time = 0;
    kernel_proc->ipc_wait_from = -1;
    kernel_proc->ipc_reply_ready = 0;
    kernel_proc->exit_code = 0;
    kernel_proc->waitpid_target = -1;
    kernel_proc->env[0] = 0;
    strncpy(kernel_proc->name, "kernel", 15);
    kernel_proc->name[15] = 0;
    current_process = kernel_proc;
    ready_queue = kernel_proc;
    KLOG_SCHED(LOG_INFO, "scheduler initialized, kernel pid=0");
}
void k_scheduler_update_sleeps() {
    if (!ready_queue)
        return;
    uint64_t now = get_uptime_ms();
    process_t *curr = ready_queue;
    do {
        if (curr->state == PROCESS_BLOCKED && curr->wake_up_time > 0) {
            if (now >= curr->wake_up_time) {
                curr->state = PROCESS_READY;
                curr->wake_up_time = 0;
            }
        }
        curr = curr->next;
    } while (curr != ready_queue);
    void sys_alarm_tick(uint64_t now_ms);
    sys_alarm_tick(now);
}
void k_scheduler_yield() {
    if (!current_process)
        return;
    process_t *old_proc = current_process;
    process_t *next_proc = old_proc->next;
    if (!next_proc)
        return;

    int max_iter = 256;
    while (next_proc->state != PROCESS_READY) {
        next_proc = next_proc->next;
        if (!next_proc || next_proc == old_proc)
            return;
        if (--max_iter <= 0) {
            KLOG_SCHED(LOG_CRIT, "process list corrupt: >256 iterations in yield");
            kernel_panic("scheduler: circular list corruption");
        }
    }
    if (next_proc == old_proc)
        return;
    if (old_proc->state == PROCESS_RUNNING) {
        old_proc->state = PROCESS_READY;
    }
    current_process = next_proc;
    current_process->state = PROCESS_RUNNING;
#if defined(__aarch64__)
    {
        uintptr_t new_pd = current_process->pd_phys;
        uintptr_t new_kstack = current_process->kernel_stack;
        uintptr_t new_esp = current_process->esp;
        arm_set_ttbr0((uint32_t)new_pd);
        arm_tlb_flush_all();

        extern volatile int hvf_ttbr0_changed;
        hvf_ttbr0_changed = 1;
        extern void hvf_sync_pagetables(void);
        hvf_sync_pagetables();
        arch_tss_set_stack(new_kstack);
        switch_context(&old_proc->esp, new_esp);
        validate_current("yield-post");
    }
#elif defined(__arm__)
    {
        uint32_t new_pd = current_process->pd_phys;
        uint32_t new_kstack = current_process->kernel_stack;
        uint32_t new_esp = current_process->esp;
        arm_set_ttbr0(new_pd);
        arm_tlb_flush_all();
        extern void hvf_sync_pagetables(void);
        hvf_sync_pagetables();
        arch_tss_set_stack(new_kstack);
        switch_context(&old_proc->esp, new_esp);
    }
#else
    asm volatile("mov %0, %%cr3" ::"r"(current_process->pd_phys));
    arch_tss_set_stack(current_process->kernel_stack);
    switch_context(&old_proc->esp, current_process->esp);
#endif
    if (process_to_free) {
        if (process_to_free->pd_phys) {
#if defined(__aarch64__)
            mm_pmm_free_frame((uint32_t)process_to_free->pd_phys);
#elif defined(__arm__)
            for (int i = 0; i < 4; i++)
                mm_pmm_free_frame(process_to_free->pd_phys + i * 4096);
#else
            mm_pmm_free_frame(process_to_free->pd_phys);
#endif
        }
        if (process_to_free->kstack_alloc)
            mm_heap_free((void *)(uintptr_t)process_to_free->kstack_alloc);
        mm_heap_free(process_to_free);
        process_to_free = 0;
    }
}
process_t *k_proc_find_by_id(int pid) {
    if (!ready_queue)
        return 0;
    process_t *curr = ready_queue;
    do {
        if (curr->pid == pid)
            return curr;
        curr = curr->next;
    } while (curr != ready_queue);
    return 0;
}
void block_current_process(uint32_t ms) {
    current_process->state = PROCESS_BLOCKED;
    current_process->wake_up_time = get_uptime_ms() + ms;
    k_scheduler_yield();
}
process_t *k_proc_spawn(uint8_t *code, uint32_t size) {
    if (!code || size < 4)
        return 0;
    int temp_code_freed = 0;
    uint8_t *temp_code = (uint8_t *)mm_heap_alloc(size);
    if (!temp_code) {
        KLOG_SCHED(LOG_ERROR, "spawn: OOM allocating %u bytes for code copy", size);
        return 0;
    }
    memcpy(temp_code, code, size);
    exec_format_t format = EXEC_FMT_NONE;
    QuarkExec *exec = 0;
    if (size >= sizeof(QuarkExec)) {
        exec = (QuarkExec *)temp_code;
        if (exec->magic == 0x5845517F)
            format = EXEC_FMT_QEX;
    }
    if (format == EXEC_FMT_NONE && elf_validate(temp_code, size)) {
        format = EXEC_FMT_ELF;
    }
    if (format == EXEC_FMT_NONE) {
        KLOG_SCHED(LOG_WARN, "spawn: unknown executable format (size=%u)", size);
        mm_heap_free(temp_code);
        return 0;
    }
    process_t *proc = (process_t *)mm_heap_alloc(sizeof(process_t));
    if (!proc) {
        KLOG_SCHED(LOG_ERROR, "spawn: OOM allocating process_t");
        mm_heap_free(temp_code);
        return 0;
    }
    proc->pid = next_pid++;
    proc->io_privilege_mask = 0;
    proc->ipc_queue.head = 0;
    proc->ipc_queue.tail = 0;
    proc->ipc_queue.count = 0;
    proc->ipc_lock = (spinlock_t)SPINLOCK_INIT;
    proc->wake_up_time = 0;
    proc->ipc_wait_from = -1;
    proc->ipc_reply_ready = 0;
    proc->exit_code = 0;
    proc->waitpid_target = -1;
    if (current_process)
        memcpy(proc->env, current_process->env, 256);
    else
        proc->env[0] = 0;
    proc->name[0] = 0;
    proc->heap_end = 0;
    proc->image_base = 0;
    proc->image_end = 0;
    proc->kernel_stack = 0;
    proc->kstack_alloc = 0;
    proc->esp = 0;
#if defined(__aarch64__)

    uint32_t new_pd_phys = mm_pmm_alloc_frame();
#elif defined(__arm__)

    extern uint32_t mm_pmm_alloc_l1(void);
    uint32_t new_pd_phys = mm_pmm_alloc_l1();
#else
    uint32_t new_pd_phys = mm_pmm_alloc_frame();
#endif
    if (!new_pd_phys) {
        mm_heap_free(proc);
        mm_heap_free(temp_code);
        return 0;
    }
#if defined(__aarch64__)

    uint64_t *new_pd_virt64 = (uint64_t *)(uintptr_t)(new_pd_phys + 0x80000000U);
    memset(new_pd_virt64, 0, 4096);

    extern uint64_t aarch64_kernel_l1[];
    new_pd_virt64[3] = aarch64_kernel_l1[3];

    for (int ci = 0; ci < 4096; ci += 64)
        asm volatile("dc civac, %0" ::"r"((uint64_t)(uintptr_t)new_pd_virt64 + ci) : "memory");
    asm volatile("dsb sy" ::: "memory");
    extern void mm_vmm_set_l1_override(uint64_t *);
    mm_vmm_set_l1_override(new_pd_virt64);
#elif defined(__arm__)

    uint32_t *new_pd_virt = (uint32_t *)(new_pd_phys + 0x80000000);
    memset(new_pd_virt, 0, 16384);

    extern uint32_t arm_kernel_l1[4096];
    for (int i = 3072; i < 4096; i++)
        new_pd_virt[i] = arm_kernel_l1[i];
    uint32_t old_cr3 = arm_get_ttbr0();
    arm_set_ttbr0(new_pd_phys);
    arm_tlb_flush_all();
#else
    uint32_t temp_map_addr = 0xE0000000;
    mm_vmm_map_page(temp_map_addr, new_pd_phys, PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE);
    uint32_t *new_pd_virt = (uint32_t *)temp_map_addr;
    memset(new_pd_virt, 0, 4096);
    uint32_t *current_pd = (uint32_t *)0xFFFFF000;
    for (int i = 768; i < 1023; i++) {
        new_pd_virt[i] = current_pd[i];
    }
    new_pd_virt[temp_map_addr >> 22] = 0;
    new_pd_virt[1023] = new_pd_phys | PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE;
    uint32_t old_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(old_cr3));
    asm volatile("mov %0, %%cr3" ::"r"(new_pd_phys));
#endif
#if defined(__aarch64__)

    void *kstack_virt = mm_heap_alloc(KSTACK_SIZE + KSTACK_GUARD);
    if (!kstack_virt) {
        KLOG_SCHED(LOG_ERROR, "spawn pid=%d: OOM allocating kernel stack", proc->pid);
        goto spawn_fail;
    }
    proc->kstack_alloc = (uintptr_t)kstack_virt;

    proc->kernel_stack = ((uintptr_t)kstack_virt + KSTACK_SIZE) & ~0xFUL;
#else
    void *kstack_virt = mm_heap_alloc(KSTACK_SIZE);
    if (!kstack_virt) {
        goto spawn_fail;
    }
    proc->kstack_alloc = (uint32_t)(uintptr_t)kstack_virt;
    proc->kernel_stack = ((uint32_t)(uintptr_t)kstack_virt + KSTACK_SIZE) & ~0xFU;
#endif
    proc->state = PROCESS_READY;
    proc->waiting_irq = -1;
    uint32_t user_stack_virt = 0xB0000000;
    uint32_t user_stack_pages = 16;
    uint32_t user_stack_size = user_stack_pages * 4096;
    uint32_t flags = PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE | PAGE_FLAG_USER;
    for (uint32_t s = 0; s < user_stack_pages; s++) {
        uint32_t user_stack_phys = mm_pmm_alloc_frame();
        if (!user_stack_phys) {
            goto spawn_fail;
        }
        mm_vmm_map_page(user_stack_virt + s * 4096, user_stack_phys, flags);
#if defined(__arm__) || defined(__aarch64__)

        memset((void *)(uintptr_t)(user_stack_phys + 0x80000000U), 0, 4096);
#endif
    }
#if !defined(__arm__) && !defined(__aarch64__)
    memset((void *)user_stack_virt, 0, user_stack_size);
#endif
    exec_image_info_t image_info;
    int load_res = -1;
    if (format == EXEC_FMT_QEX) {
        load_res = load_quark_exec_image(exec, size, &image_info);
    } else if (format == EXEC_FMT_ELF) {
        load_res = elf_load_image(temp_code, size, &image_info);
    }
    if (load_res != 0) {
        goto spawn_fail;
    }
    mm_heap_free(temp_code);
    temp_code_freed = 1;
#if defined(__aarch64__)

    arm_dsb();
    asm volatile("ic iallu" ::: "memory");
    arm_dsb();
    arm_isb();
    {
        extern void mm_vmm_clear_l1_override(void);
        mm_vmm_clear_l1_override();
    }
#elif defined(__arm__)

    asm volatile("mcr p15, 0, %0, c7, c10, 0" ::"r"(0) : "memory");
    arm_dsb();
    asm volatile("mcr p15, 0, %0, c7, c5, 0" ::"r"(0) : "memory");
    arm_dsb();
    arm_isb();
    arm_set_ttbr0(old_cr3);
    arm_tlb_flush_all();
#else
    asm volatile("mov %0, %%cr3" ::"r"(old_cr3));
    {
        uint32_t *ptable = Rec_PAGEDIR(temp_map_addr >> 22);
        ptable[(temp_map_addr >> 12) & 0x3FF] = 0;
        mm_vmm_tlb_flush_single(temp_map_addr);
    }
#endif
    proc->pd_phys = new_pd_phys;
    proc->heap_end = (image_info.image_end + 0xFFF) & ~0xFFF;
    proc->image_base = image_info.image_base;
    proc->image_end = proc->heap_end;
    proc->shared_virt_bump = 0x88000000;
#if defined(__aarch64__)

    {
        uint64_t *sp64 = (uint64_t *)proc->kernel_stack;

        *--sp64 = (uint64_t)(user_stack_virt + user_stack_size);
        *--sp64 = (uint64_t)image_info.entry;

        sp64 -= 14;

        sp64[10] = 0;
        sp64[11] = (uint64_t)(uintptr_t)irq_ret_stub;

        for (int i = 0; i < 10; i++)
            sp64[i] = 0;

        sp64[12] = 0;
        sp64[13] = 0;
        proc->esp = (uintptr_t)sp64;
    }
#elif defined(__arm__)
    {
        uint32_t *esp = (uint32_t *)proc->kernel_stack;

        *--esp = user_stack_virt + user_stack_size;
        *--esp = image_info.entry;
        *--esp = (uint32_t)irq_ret_stub;
        *--esp = 0;
        *--esp = 0;
        *--esp = 0;
        *--esp = 0;
        *--esp = 0;
        *--esp = 0;
        *--esp = 0;
        *--esp = 0;
        proc->esp = (uint32_t)esp;
    }
#else
    {
        uint32_t *esp = (uint32_t *)proc->kernel_stack;
        *--esp = 0x23;
        *--esp = user_stack_virt + user_stack_size;
        *--esp = 0x202;
        *--esp = 0x1B;
        *--esp = image_info.entry;
        *--esp = (uint32_t)irq_ret_stub;
        *--esp = 0;
        *--esp = 0;
        *--esp = 0;
        *--esp = 0;
        *--esp = 0x202;
        proc->esp = (uint32_t)esp;
    }
#endif
    proc->next = ready_queue->next;
    ready_queue->next = proc;
    KLOG_SCHED(LOG_INFO, "spawned pid=%d entry=%x kstack=%x-%x", proc->pid, image_info.entry,
               (uint32_t)proc->kstack_alloc, (uint32_t)proc->kernel_stack);
    return proc;
spawn_fail:
#if defined(__aarch64__)

{
    extern void mm_vmm_clear_l1_override(void);
    mm_vmm_clear_l1_override();
}

    mm_pmm_free_frame(new_pd_phys);
#elif defined(__arm__)
    arm_set_ttbr0(old_cr3);
    arm_tlb_flush_all();

    for (int fi = 0; fi < 4; fi++)
        mm_pmm_free_frame(new_pd_phys + fi * 4096);
#else
    asm volatile("mov %0, %%cr3" ::"r"(old_cr3));
    {
        uint32_t *ptable = Rec_PAGEDIR(temp_map_addr >> 22);
        ptable[(temp_map_addr >> 12) & 0x3FF] = 0;
        mm_vmm_tlb_flush_single(temp_map_addr);
    }
    mm_pmm_free_frame(new_pd_phys);
#endif
    if (!temp_code_freed)
        mm_heap_free(temp_code);
    if (proc->kernel_stack) {
        mm_heap_free((void *)(uintptr_t)proc->kstack_alloc);
    }
    mm_heap_free(proc);
    return 0;
}
void k_proc_terminate(int pid) {
    if (pid == 0)
        return;

    process_t *prev = ready_queue;
    process_t *curr = ready_queue->next;
    while (curr && curr != ready_queue) {
        if (curr->pid == pid) {
            KLOG_PROC(LOG_WARN, "TERMINATE pid=%d name='%s' state=%d by=%d", pid, curr->name,
                      (int)curr->state, current_process ? current_process->pid : -1);
            void k_notify_server_proc_exit(int pid);
            k_notify_server_proc_exit(pid);
            {
                process_t *scanner = ready_queue;
                do {
                    if (scanner->waitpid_target == pid) {
                        scanner->ipc_reply_slot.data1 = (uint32_t)curr->exit_code;
                        scanner->waitpid_target = -1;
                        scanner->state = PROCESS_READY;
                    }
                    scanner = scanner->next;
                } while (scanner && scanner != ready_queue);
            }
            prev->next = curr->next;
            if (current_process == curr) {
                KLOG_PROC(LOG_WARN, "KILL-SELF pid=%d → cleanup + yield", pid);
                mm_vmm_cleanup_current_dir();

                curr->state = PROCESS_ZOMBIE;
                process_to_free = curr;
                k_scheduler_yield();
            } else {
                if (curr->state != PROCESS_ZOMBIE) {
                    mm_vmm_cleanup_dir_phys((uint32_t)curr->pd_phys);
                    if (curr->pd_phys)
                        mm_pmm_free_frame((uint32_t)curr->pd_phys);
                    if (curr->kstack_alloc)
                        mm_heap_free((void *)(uintptr_t)curr->kstack_alloc);
                    mm_heap_free(curr);
                }
            }
            return;
        }
        prev = curr;
        curr = curr->next;
    }
    KLOG_PROC(LOG_ERROR, "KILL-NOTFOUND pid=%d — not in ready_queue!", pid);
}
void k_scheduler_direct_switch(process_t *dest) {
    if (!dest || dest == current_process)
        return;
    process_t *old = current_process;
    if (old->state == PROCESS_RUNNING)
        old->state = PROCESS_READY;
    current_process = dest;
    dest->state = PROCESS_RUNNING;
#if defined(__aarch64__)
    {
        uintptr_t new_pd = dest->pd_phys;
        uintptr_t new_kstack = dest->kernel_stack;
        uintptr_t new_esp = dest->esp;
        arm_set_ttbr0((uint32_t)new_pd);
        arm_tlb_flush_all();
        extern volatile int hvf_ttbr0_changed;
        hvf_ttbr0_changed = 1;
        extern void hvf_sync_pagetables(void);
        hvf_sync_pagetables();
        arch_tss_set_stack(new_kstack);
        switch_context(&old->esp, new_esp);
        validate_current("direct-post");
    }
#elif defined(__arm__)
    {
        uint32_t new_pd = dest->pd_phys;
        uint32_t new_kstack = dest->kernel_stack;
        uint32_t new_esp = dest->esp;
        arm_set_ttbr0(new_pd);
        arm_tlb_flush_all();
        extern void hvf_sync_pagetables(void);
        hvf_sync_pagetables();
        arch_tss_set_stack(new_kstack);
        switch_context(&old->esp, new_esp);
    }
#else
    asm volatile("mov %0, %%cr3" ::"r"(dest->pd_phys));
    arch_tss_set_stack(dest->kernel_stack);
    switch_context(&old->esp, dest->esp);
#endif
    if (process_to_free) {
        if (process_to_free->pd_phys) {
#if defined(__aarch64__)
            mm_pmm_free_frame((uint32_t)process_to_free->pd_phys);
#elif defined(__arm__)
            for (int i = 0; i < 4; i++)
                mm_pmm_free_frame(process_to_free->pd_phys + i * 4096);
#else
            mm_pmm_free_frame(process_to_free->pd_phys);
#endif
        }
        if (process_to_free->kstack_alloc)
            mm_heap_free((void *)(uintptr_t)process_to_free->kstack_alloc);
        mm_heap_free(process_to_free);
        process_to_free = 0;
    }
}
void k_proc_wait_irq(int irq) {
    current_process->state = PROCESS_WAIT_IRQ;
    current_process->waiting_irq = irq;
    k_scheduler_yield();
}
void process_wake_on_irq(int irq) {
    if (!ready_queue)
        return;
    process_t *curr = ready_queue;
    do {
        if (curr->state == PROCESS_WAIT_IRQ) {
            int wait_irq = curr->waiting_irq;
            int same_irq = (wait_irq == irq);
            int paired = ((wait_irq == 1 && irq == 12) || (wait_irq == 12 && irq == 1));
            if (same_irq || paired) {
                curr->state = PROCESS_READY;
                curr->waiting_irq = -1;
            }
        }
        curr = curr->next;
    } while (curr != ready_queue);
}
