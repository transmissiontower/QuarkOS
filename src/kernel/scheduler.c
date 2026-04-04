#include "arch/x86/gdt.h"
#include "drivers/GUI/framebuffer.h"
#include "kernel/elf.h"
#include "kernel/process.h"
#include "lib/stdlib.h"
#include "mm/kmalloc.h"
#include "mm/memory.h"
#include "util/util.h"

#define KSTACK_SIZE 4096

process_t *current_process = 0;
process_t *ready_queue = 0;
static int next_pid = 1;

extern void switch_context(uint32_t *old_esp, uint32_t new_esp);
extern void irq_ret_stub();
extern uint64_t get_uptime_ms();
void mm_pmm_free_frame(uint32_t phys);
void mm_vmm_cleanup_current_dir();
static process_t *process_to_free = 0;

typedef enum { EXEC_FMT_NONE = 0, EXEC_FMT_QEX = 1, EXEC_FMT_ELF = 2 } exec_format_t;

static int load_quark_exec_image(const QuarkExec *exec, uint32_t size, exec_image_info_t *info)
{
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
    for (uint32_t i = 0; i < pages_needed; i++) {
        uint32_t phys = mm_pmm_alloc_frame();
        if (!phys)
            return -1;
        mm_vmm_map_page(user_base + (i * 4096), phys, flags);
    }

    const uint8_t *payload = ((const uint8_t *)exec) + sizeof(QuarkExec);
    memcpy((void *)user_base, payload, payload_size);
    if (exec->bss_size)
        memset((void *)(user_base + payload_size), 0, exec->bss_size);

    info->entry = exec->entry;
    info->image_base = user_base;
    info->image_end = user_base + total_mem_size;
    if (info->entry < info->image_base || info->entry >= info->image_end)
        return -1;
    return 0;
}

void k_scheduler_init()
{
    process_t *kernel_proc = (process_t *)mm_heap_alloc(sizeof(process_t));
    kernel_proc->pid = 0;
    uint32_t pd_phys;
    asm volatile("mov %%cr3, %0" : "=r"(pd_phys));
    kernel_proc->pd_phys = pd_phys;
    kernel_proc->state = PROCESS_RUNNING;
    kernel_proc->next = kernel_proc;
    kernel_proc->io_privilege_mask = 0xFFFFFFFF;
    kernel_proc->ipc_queue.head = 0;
    kernel_proc->ipc_queue.tail = 0;
    kernel_proc->ipc_queue.count = 0;
    kernel_proc->wake_up_time = 0;
    kernel_proc->ipc_wait_from = -1;
    kernel_proc->ipc_reply_ready = 0;
    strncpy(kernel_proc->name, "kernel", 15);
    kernel_proc->name[15] = 0;

    current_process = kernel_proc;
    ready_queue = kernel_proc;
}

void k_scheduler_update_sleeps()
{
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
}

void k_scheduler_yield()
{
    if (!current_process)
        return;

    process_t *old_proc = current_process;
    process_t *next_proc = old_proc->next;

    while (next_proc->state != PROCESS_READY) {
        next_proc = next_proc->next;
        if (next_proc == old_proc)
            return;
    }

    if (next_proc == old_proc)
        return;

    if (old_proc->state == PROCESS_RUNNING) {
        old_proc->state = PROCESS_READY;
    }

    current_process = next_proc;
    current_process->state = PROCESS_RUNNING;

    asm volatile("mov %0, %%cr3" ::"r"(current_process->pd_phys));

    arch_tss_set_stack(current_process->kernel_stack);

    switch_context(&old_proc->esp, current_process->esp);

    if (process_to_free) {
        if (process_to_free->pd_phys) {
            mm_pmm_free_frame(process_to_free->pd_phys);
        }
        mm_heap_free((void *)(process_to_free->kernel_stack - KSTACK_SIZE));
        mm_heap_free(process_to_free);
        process_to_free = 0;
    }
}

process_t *k_proc_find_by_id(int pid)
{
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

void block_current_process(uint32_t ms)
{
    current_process->state = PROCESS_BLOCKED;
    current_process->wake_up_time = get_uptime_ms() + ms;
    k_scheduler_yield();
}

process_t *k_proc_spawn(uint8_t *code, uint32_t size)
{
    if (!code || size < 4)
        return 0;
    int temp_code_freed = 0;

    uint8_t *temp_code = (uint8_t *)mm_heap_alloc(size);
    if (!temp_code)
        return 0;
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
        mm_heap_free(temp_code);
        return 0;
    }

    process_t *proc = (process_t *)mm_heap_alloc(sizeof(process_t));
    if (!proc) {
        mm_heap_free(temp_code);
        return 0;
    }
    proc->pid = next_pid++;
    proc->io_privilege_mask = 0;
    proc->ipc_queue.head = 0;
    proc->ipc_queue.tail = 0;
    proc->ipc_queue.count = 0;
    proc->wake_up_time = 0;
    proc->ipc_wait_from = -1;
    proc->ipc_reply_ready = 0;
    proc->name[0] = 0;
    proc->heap_end = 0;
    proc->image_base = 0;
    proc->image_end = 0;
    proc->kernel_stack = 0;
    proc->esp = 0;

    uint32_t new_pd_phys = mm_pmm_alloc_frame();
    if (!new_pd_phys) {
        mm_heap_free(proc);
        mm_heap_free(temp_code);
        return 0;
    }

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

    void *kstack_virt = mm_heap_alloc(KSTACK_SIZE);
    if (!kstack_virt) {
        goto spawn_fail;
    }
    proc->kernel_stack = (uint32_t)kstack_virt + KSTACK_SIZE;

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
    }
    memset((void *)user_stack_virt, 0, user_stack_size);

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
    asm volatile("mov %0, %%cr3" ::"r"(old_cr3));

    {
        uint32_t *ptable = Rec_PAGEDIR(temp_map_addr >> 22);
        ptable[(temp_map_addr >> 12) & 0x3FF] = 0;
        mm_vmm_tlb_flush_single(temp_map_addr);
    }
    proc->pd_phys = new_pd_phys;
    proc->heap_end = (image_info.image_end + 0xFFF) & ~0xFFF;
    proc->image_base = image_info.image_base;
    proc->image_end = proc->heap_end;

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

    proc->next = ready_queue->next;
    ready_queue->next = proc;
    return proc;

spawn_fail:
    mm_vmm_cleanup_current_dir();
    asm volatile("mov %0, %%cr3" ::"r"(old_cr3));

    {
        uint32_t *ptable = Rec_PAGEDIR(temp_map_addr >> 22);
        ptable[(temp_map_addr >> 12) & 0x3FF] = 0;
        mm_vmm_tlb_flush_single(temp_map_addr);
    }
    mm_pmm_free_frame(new_pd_phys);
    if (!temp_code_freed)
        mm_heap_free(temp_code);
    if (proc->kernel_stack) {
        mm_heap_free((void *)(proc->kernel_stack - KSTACK_SIZE));
    }
    mm_heap_free(proc);
    return 0;
}

void k_proc_terminate(int pid)
{
    if (pid == 0)
        return;

    process_t *prev = ready_queue;
    process_t *curr = ready_queue->next;

    while (curr != ready_queue) {
        if (curr->pid == pid) {
            void k_notify_server_proc_exit(int pid);
            k_notify_server_proc_exit(pid);

            prev->next = curr->next;

            if (current_process == curr) {
                mm_vmm_cleanup_current_dir();
                mm_pmm_free_frame(curr->pd_phys);
                curr->pd_phys = 0;
                curr->state = PROCESS_ZOMBIE;
                process_to_free = curr;
                k_scheduler_yield();
            } else {

                mm_heap_free((void *)(curr->kernel_stack - KSTACK_SIZE));
                mm_heap_free(curr);
            }

            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

void k_scheduler_direct_switch(process_t *dest)
{
    if (!dest || dest == current_process)
        return;

    process_t *old = current_process;
    if (old->state == PROCESS_RUNNING)
        old->state = PROCESS_READY;

    current_process = dest;
    dest->state = PROCESS_RUNNING;

    asm volatile("mov %0, %%cr3" ::"r"(dest->pd_phys));
    arch_tss_set_stack(dest->kernel_stack);
    switch_context(&old->esp, dest->esp);

    if (process_to_free) {
        if (process_to_free->pd_phys)
            mm_pmm_free_frame(process_to_free->pd_phys);
        mm_heap_free((void *)(process_to_free->kernel_stack - KSTACK_SIZE));
        mm_heap_free(process_to_free);
        process_to_free = 0;
    }
}

void k_proc_wait_irq(int irq)
{
    current_process->state = PROCESS_WAIT_IRQ;
    current_process->waiting_irq = irq;
    k_scheduler_yield();
}

void process_wake_on_irq(int irq)
{
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
