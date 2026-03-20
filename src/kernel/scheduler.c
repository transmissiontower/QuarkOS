#include "kernel/process.h"
#include "mm/kmalloc.h"
#include "mm/memory.h"
#include "util/util.h"
#include "arch/x86/gdt.h"
#include "drivers/GUI/framebuffer.h"

process_t *current_process = 0;
process_t *ready_queue = 0;
static int next_pid = 1;

extern void switch_context(uint32_t *old_esp, uint32_t new_esp);
extern void irq_ret_stub();
extern uint64_t get_uptime_ms();
void mm_pmm_free_frame(uint32_t phys);
void mm_vmm_cleanup_current_dir();
static process_t *process_to_free = 0;

void k_scheduler_init(){
    process_t *kernel_proc = (process_t*)mm_heap_alloc(sizeof(process_t));
    kernel_proc->pid = 0;
    kernel_proc->pd_phys = (uint32_t)mem_get_current_page_dir() - KERNEL_START;
    kernel_proc->state = PROCESS_RUNNING;
    kernel_proc->next = kernel_proc;
    kernel_proc->io_privilege_mask = 0xFFFFFFFF;
    kernel_proc->ipc_queue.head = 0;
    kernel_proc->ipc_queue.tail = 0;
    kernel_proc->ipc_queue.count = 0;
    kernel_proc->wake_up_time = 0;
    strncpy(kernel_proc->name, "kernel", 15);
    kernel_proc->name[15] = 0;

    current_process = kernel_proc;
    ready_queue = kernel_proc;
}

void k_scheduler_update_sleeps() {
    if (!ready_queue) return;

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

void k_scheduler_yield() {
    if (!current_process) return;

    process_t *old_proc = current_process;
    process_t *next_proc = old_proc->next;

    while (next_proc->state != PROCESS_READY) {
        next_proc = next_proc->next;
        if (next_proc == old_proc) return;
    }

    if (next_proc == old_proc) return;

    if (old_proc->state == PROCESS_RUNNING) {
        old_proc->state = PROCESS_READY;
    }

    current_process = next_proc;
    current_process->state = PROCESS_RUNNING;

    asm volatile("mov %0, %%cr3" :: "r"(current_process->pd_phys));

    arch_tss_set_stack(current_process->kernel_stack);

    switch_context(&old_proc->esp, current_process->esp);

    if (process_to_free) {
        if (process_to_free->pd_phys) {
            
        }
        mm_heap_free((void*)(process_to_free->kernel_stack - 4096));
        mm_heap_free(process_to_free);
        process_to_free = 0;
    }
}

process_t* k_proc_find_by_id(int pid) {
    process_t *curr = current_process;
    do {
        if (curr->pid == pid) return curr;
        curr = curr->next;
    } while (curr != current_process);
    return 0;
}

void block_current_process(uint32_t ms) {
    current_process->state = PROCESS_BLOCKED;
    current_process->wake_up_time = get_uptime_ms() + ms;
    k_scheduler_yield();
}

void k_proc_spawn(uint8_t *code, uint32_t size) {
    if (size < sizeof(QuarkExec)) return;

    uint8_t *temp_code = (uint8_t*)mm_heap_alloc(size);
    if (!temp_code) return;
    memcpy(temp_code, code, size);

    QuarkExec *exec = (QuarkExec*)temp_code;
    if (exec->magic != 0x5845517F) {
        mm_heap_free(temp_code);
        return;
    }

    process_t *proc = (process_t*)mm_heap_alloc(sizeof(process_t));
    proc->pid = next_pid++;
    proc->io_privilege_mask = 0;
    proc->ipc_queue.head = 0;
    proc->ipc_queue.tail = 0;
    proc->ipc_queue.count = 0;
    proc->wake_up_time = 0;
    proc->name[0] = 0;

    uint32_t new_pd_phys = mm_pmm_alloc_frame();
    if (!new_pd_phys) return;

    uint32_t temp_map_addr = 0xE0000000;
    mm_vmm_map_page(temp_map_addr, new_pd_phys, PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE);

    uint32_t *new_pd_virt = (uint32_t*)temp_map_addr;
    memset(new_pd_virt, 0, 4096);

    uint32_t *current_pd = (uint32_t*)0xFFFFF000;
    for (int i = 768; i < 1023; i++) {
        new_pd_virt[i] = current_pd[i];
    }

    new_pd_virt[1023] = new_pd_phys | PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE;

    uint32_t old_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(old_cr3));
    asm volatile("mov %0, %%cr3" :: "r"(new_pd_phys));

    uint32_t kstack_size = 4096;
    uint32_t kstack_phys = mm_pmm_alloc_frame();
    if (!kstack_phys) {
        
        return;
    }
    void* kstack_virt = mm_heap_alloc(kstack_size);
    proc->kernel_stack = (uint32_t)kstack_virt + kstack_size;

    proc->state = PROCESS_READY;
    proc->waiting_irq = -1;

    uint32_t user_code_virt = 0x400000;
    uint32_t user_stack_virt = 0xB0000000;

    uint32_t flags = PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE | PAGE_FLAG_USER;

    uint32_t user_stack_phys = mm_pmm_alloc_frame();
    if (!user_stack_phys) return;
    mm_vmm_map_page(user_stack_virt, user_stack_phys, flags);
    memset((void*)user_stack_virt, 0, 4096);

    uint32_t code_payload_size = size - sizeof(QuarkExec);
    uint32_t total_mem_size = code_payload_size + exec->bss_size;
    uint32_t pages_needed = (total_mem_size + 4095) / 4096;

    for (uint32_t i = 0; i < pages_needed; i++) {
        uint32_t code_phys = mm_pmm_alloc_frame();
        if (!code_phys) return;
        mm_vmm_map_page(user_code_virt + (i * 4096), code_phys, flags);
    }

    memcpy((void*)user_code_virt, temp_code + sizeof(QuarkExec), code_payload_size);
    memset((void*)(user_code_virt + code_payload_size), 0, exec->bss_size);

    mm_heap_free(temp_code);

    asm volatile("mov %0, %%cr3" :: "r"(old_cr3));
    proc->pd_phys = new_pd_phys;
    proc->heap_end = user_code_virt + pages_needed * 4096;

    uint32_t *esp = (uint32_t*)proc->kernel_stack;

    *--esp = 0x23;
    *--esp = user_stack_virt + 4096;
    *--esp = 0x202;
    *--esp = 0x1B;
    *--esp = exec->entry;
    *--esp = (uint32_t)irq_ret_stub;

    *--esp = 0;
    *--esp = 0;
    *--esp = 0;
    *--esp = 0;
    *--esp = 0x202;

    proc->esp = (uint32_t)esp;

    proc->next = ready_queue->next;
    ready_queue->next = proc;
}

void k_proc_terminate(int pid) {
    if (pid == 0) return;

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
                
                
                mm_heap_free((void*)(curr->kernel_stack - 4096));
                mm_heap_free(curr);
            }

            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

void k_proc_wait_irq(int irq) {
    current_process->state = PROCESS_WAIT_IRQ;
    current_process->waiting_irq = irq;
    k_scheduler_yield();
}

void process_wake_on_irq(int irq) {
    if (!ready_queue) return;

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
    } while(curr != ready_queue);
}
