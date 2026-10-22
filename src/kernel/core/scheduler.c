#include "arch/aarch64/platform.h"
#include "drivers/video/framebuffer.h"
#include "kernel/core/klog.h"
#include "kernel/core/process.h"
#include "stdlib.h"
#include "kernel/mm/kmalloc.h"
#include "kernel/mm/memory.h"
#include "kernel/lib/util.h"
#define KSTACK_SIZE 32768
#define KSTACK_GUARD 4096
process_t *volatile current_process = 0;
process_t *ready_queue = 0;
static int next_pid = 1;
__attribute__((returns_twice)) extern void switch_context(uintptr_t *old_esp, uintptr_t new_esp);
extern void irq_ret_stub();
extern uint64_t get_uptime_ms();
void mm_pmm_free_frame(uint32_t phys);
void mm_vmm_cleanup_current_dir();
static process_t *process_to_free = 0;

static void reap_pending(void) {
    process_t *p = process_to_free;
    if (!p || p == current_process)
        return;
    process_to_free = 0;
    if (p->pd_phys)
        mm_pmm_free_frame((uint32_t)p->pd_phys);
    if (p->kstack_alloc)
        mm_heap_free((void *)(uintptr_t)p->kstack_alloc);
    mm_heap_free(p);
}

void validate_current(const char *tag) {
    uintptr_t cp = (uintptr_t)current_process;

    if (cp < 0xD0000000UL || cp >= 0xE0000000UL) {
        KLOG_PROC(LOG_CRIT, "CORRUPT current_process=%lx at %s", (uint64_t)cp, tag);
        extern void kernel_panic(const char *msg);
        kernel_panic("current_process out of range");
    }
    int pid = current_process->pid;
    if (pid < 0) { /* PIDs are never reused, so there is no upper bound */
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

typedef struct {
    uint64_t entry;
    uint32_t image_base;
    uint32_t image_end;
} exec_image_info_t;

#define MAX_USER_IMAGE 0x4000000U /* 64 MiB */

typedef struct __attribute__((packed)) {
    unsigned char ident[16]; uint16_t type, machine; uint32_t version;
    uint64_t entry, phoff, shoff; uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} elf64_ehdr_t;
typedef struct __attribute__((packed)) {
    uint32_t type, flags; uint64_t offset, vaddr, paddr, filesz, memsz, align;
} elf64_phdr_t;

static int load_elf_image(const uint8_t *elf, uint32_t size, uint32_t pd_phys, exec_image_info_t *info) {
    if (!elf || !info || size < sizeof(elf64_ehdr_t))
        return -1;
    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)elf;
    if (eh->ident[0] != 0x7f || eh->ident[1] != 'E' || eh->ident[2] != 'L' ||
        eh->ident[3] != 'F' || eh->ident[4] != 2 || eh->ident[5] != 1 || eh->ident[6] != 1 ||
        eh->version != 1 || eh->machine != 183 || eh->type != 2 ||
        eh->ehsize < sizeof(elf64_ehdr_t) || eh->phentsize < sizeof(elf64_phdr_t) ||
        !eh->phnum || eh->entry > UINT32_MAX || (eh->entry & 3) ||
        eh->phoff > size || (uint64_t)eh->phnum * eh->phentsize > size - eh->phoff)
        return -1;
    uint64_t low = UINT32_MAX, high = 0;
    int entry_is_executable = 0;
    for (uint32_t i = 0; i < eh->phnum; i++) {
        const elf64_phdr_t *ph = (const elf64_phdr_t *)(elf + eh->phoff + (uint64_t)i * eh->phentsize);
        if (ph->type != 1) continue;
        if (ph->filesz > ph->memsz || ph->offset > size || ph->filesz > size - ph->offset ||
            ph->vaddr > UINT32_MAX || ph->memsz > UINT32_MAX || ph->vaddr + ph->memsz > 0x80000000ULL ||
            (ph->align > 1 && ((ph->align & (ph->align - 1)) ||
                               (ph->vaddr % ph->align) != (ph->offset % ph->align))) ||
            ((ph->vaddr ^ ph->offset) & 0xFFFU))
            return -1;
        if (ph->memsz) {
            if (ph->vaddr < low) low = ph->vaddr;
            if (ph->vaddr + ph->memsz > high) high = ph->vaddr + ph->memsz;
            if ((ph->flags & 1U) && eh->entry >= ph->vaddr && eh->entry < ph->vaddr + ph->memsz)
                entry_is_executable = 1;
        }
    }
    if (low == UINT32_MAX || high <= low || high - low > MAX_USER_IMAGE || !entry_is_executable)
        return -1;
    uint32_t first = (uint32_t)low & ~4095U, end = ((uint32_t)high + 4095U) & ~4095U;
    for (uint32_t va = first; va < end; va += 4096) {
        uint32_t page_flags = PAGE_FLAG_PRESENT | PAGE_FLAG_USER;
        int page_used = 0;
        for (uint32_t i = 0; i < eh->phnum; i++) {
            const elf64_phdr_t *ph = (const elf64_phdr_t *)(elf + eh->phoff + (uint64_t)i * eh->phentsize);
            if (ph->type != 1 || !ph->memsz || ph->vaddr >= (uint64_t)va + 4096 ||
                ph->vaddr + ph->memsz <= va)
                continue;
            page_used = 1;
            if (ph->flags & 2U) page_flags |= PAGE_FLAG_WRITE;
            if (ph->flags & 1U) page_flags |= PAGE_FLAG_EXEC;
        }
        if (!page_used) continue;
        uint32_t phys = mm_pmm_alloc_frame(); if (!phys) return -1;
        mm_vmm_map_page(va, phys, page_flags);
        uint32_t mapped = mm_user_virt_to_phys_dir(pd_phys, va);
        if (!mapped) return -1;
        memset((void *)(uintptr_t)(0x80000000U + (mapped & ~0xFFFU)), 0, 4096);
    }
    for (uint32_t i = 0; i < eh->phnum; i++) {
        const elf64_phdr_t *ph = (const elf64_phdr_t *)(elf + eh->phoff + (uint64_t)i * eh->phentsize);
        if (ph->type != 1 || !ph->filesz) continue;
        uint64_t copied = 0;
        while (copied < ph->filesz) {
            uint32_t va = (uint32_t)(ph->vaddr + copied);
            uint32_t phys = mm_user_virt_to_phys_dir(pd_phys, va);
            if (!phys) return -1;
            uint32_t chunk = 4096U - (phys & 0xFFFU);
            if (chunk > ph->filesz - copied) chunk = (uint32_t)(ph->filesz - copied);
            memcpy((void *)(uintptr_t)(0x80000000U + phys), elf + ph->offset + copied, chunk);
            copied += chunk;
        }
    }
    for (uint32_t va = first; va < end; va += 4096) {
        int page_used = 0;
        for (uint32_t i = 0; i < eh->phnum; i++) {
            const elf64_phdr_t *ph = (const elf64_phdr_t *)(elf + eh->phoff + (uint64_t)i * eh->phentsize);
            if (ph->type == 1 && ph->memsz && ph->vaddr < (uint64_t)va + 4096 &&
                ph->vaddr + ph->memsz > va) { page_used = 1; break; }
        }
        if (!page_used) continue;
        uint32_t phys = mm_user_virt_to_phys_dir(pd_phys, va);
        if (!phys) return -1;
        uintptr_t alias = (uintptr_t)(0x80000000U + (phys & ~0xFFFU));
        for (uint32_t off = 0; off < 4096; off += 64)
            asm volatile("dc cvau, %0" :: "r"((uint64_t)(alias + off)) : "memory");
    }
    info->entry = eh->entry; info->image_base = first; info->image_end = end;
    return 0;
}
void k_scheduler_init() {
    process_t *kernel_proc = (process_t *)mm_heap_alloc(sizeof(process_t));
    kernel_proc->pid = 0;
    kernel_proc->parent_pid = -1;
    kernel_proc->pd_phys = (uintptr_t)arm_get_ttbr0();
    kernel_proc->state = PROCESS_RUNNING;
    kernel_proc->next = kernel_proc;
    kernel_proc->ipc_queue.head = 0;
    kernel_proc->ipc_queue.tail = 0;
    kernel_proc->ipc_queue.count = 0;
    kernel_proc->wake_up_time = 0;
    kernel_proc->ipc_wait_from = -1;
    kernel_proc->ipc_reply_ready = 0;
    kernel_proc->exit_code = 0;
    kernel_proc->waitpid_target = -1;
    for (int i = 0; i < 3; i++) { kernel_proc->fd_kind[i] = FD_KIND_TTY; kernel_proc->fd_object[i] = 0; }
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
    {
        uintptr_t new_pd = current_process->pd_phys;
        uintptr_t new_esp = current_process->esp;
        arm_set_ttbr0((uint32_t)new_pd);
        arm_tlb_flush_all();

        extern volatile int hvf_ttbr0_changed;
        hvf_ttbr0_changed = 1;
        extern void hvf_sync_pagetables(void);
        hvf_sync_pagetables();
        switch_context(&old_proc->esp, new_esp);
        validate_current("yield-post");
    }
    reap_pending();
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
    if (size < 4 || temp_code[0] != 0x7f || temp_code[1] != 'E' || temp_code[2] != 'L' || temp_code[3] != 'F') {
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
    proc->parent_pid = current_process ? current_process->pid : 0;
    proc->ipc_queue.head = 0;
    proc->ipc_queue.tail = 0;
    proc->ipc_queue.count = 0;
    proc->wake_up_time = 0;
    proc->ipc_wait_from = -1;
    proc->ipc_reply_ready = 0;
    proc->exit_code = 0;
    proc->waitpid_target = -1;
    for (int i = 0; i < 3; i++) {
        proc->fd_kind[i] = current_process ? current_process->fd_kind[i] : FD_KIND_TTY;
        proc->fd_object[i] = current_process ? current_process->fd_object[i] : 0;
    }
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
    uint32_t new_pd_phys = mm_pmm_alloc_frame();
    if (!new_pd_phys) {
        mm_heap_free(proc);
        mm_heap_free(temp_code);
        return 0;
    }
    uint64_t *new_pd_virt64 = (uint64_t *)(uintptr_t)(new_pd_phys + 0x80000000U);
    memset(new_pd_virt64, 0, 4096);

    extern uint64_t aarch64_kernel_l1[];
    new_pd_virt64[3] = aarch64_kernel_l1[3];

    for (int ci = 0; ci < 4096; ci += 64)
        asm volatile("dc civac, %0" ::"r"((uint64_t)(uintptr_t)new_pd_virt64 + ci) : "memory");
    asm volatile("dsb sy" ::: "memory");
    extern void mm_vmm_set_l1_override(uint64_t *);
    mm_vmm_set_l1_override(new_pd_virt64);
    void *kstack_virt = mm_heap_alloc(KSTACK_SIZE + KSTACK_GUARD);
    if (!kstack_virt) {
        KLOG_SCHED(LOG_ERROR, "spawn pid=%d: OOM allocating kernel stack", proc->pid);
        goto spawn_fail;
    }
    proc->kstack_alloc = (uintptr_t)kstack_virt;

    proc->kernel_stack = ((uintptr_t)kstack_virt + KSTACK_SIZE) & ~0xFUL;
    proc->state = PROCESS_READY;
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
        memset((void *)(uintptr_t)(user_stack_phys + 0x80000000U), 0, 4096);
    }
    exec_image_info_t image_info;
    int load_res = -1;
    load_res = load_elf_image(temp_code, size, new_pd_phys, &image_info);
    if (load_res != 0) {
        goto spawn_fail;
    }
    mm_heap_free(temp_code);
    temp_code_freed = 1;
    arm_dsb();
    asm volatile("ic iallu" ::: "memory");
    arm_dsb();
    arm_isb();
    {
        extern void mm_vmm_clear_l1_override(void);
        mm_vmm_clear_l1_override();
    }
    proc->pd_phys = new_pd_phys;
    proc->heap_end = (image_info.image_end + 0xFFF) & ~0xFFF;
    proc->image_base = image_info.image_base;
    proc->image_end = proc->heap_end;
    proc->shared_virt_bump = 0x88000000;
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
    proc->next = ready_queue->next;
    ready_queue->next = proc;
    KLOG_SCHED(LOG_INFO, "spawned pid=%d entry=%x kstack=%x-%x", proc->pid, image_info.entry,
               (uint32_t)proc->kstack_alloc, (uint32_t)proc->kernel_stack);
    return proc;
spawn_fail:
{
    extern void mm_vmm_clear_l1_override(void);
    mm_vmm_clear_l1_override();
}

    mm_vmm_cleanup_dir_phys(new_pd_phys); 
    mm_pmm_free_frame(new_pd_phys);
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
            void k_pipe_process_exit(process_t *proc);
            k_pipe_process_exit(curr);
            void k_notify_server_proc_exit(int pid);
            k_notify_server_proc_exit(pid);
            {
                process_t *scanner = ready_queue;
                do {
                    if (scanner != curr && scanner->ipc_wait_from == pid) {
                        scanner->ipc_reply_ready = 2;
                        if (scanner->state == PROCESS_IPC_WAIT)
                            scanner->state = PROCESS_READY;
                    }
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
                reap_pending();
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
    {
        uintptr_t new_pd = dest->pd_phys;
        uintptr_t new_esp = dest->esp;
        arm_set_ttbr0((uint32_t)new_pd);
        arm_tlb_flush_all();
        extern volatile int hvf_ttbr0_changed;
        hvf_ttbr0_changed = 1;
        extern void hvf_sync_pagetables(void);
        hvf_sync_pagetables();
        switch_context(&old->esp, new_esp);
        validate_current("direct-post");
    }
    reap_pending();
}
