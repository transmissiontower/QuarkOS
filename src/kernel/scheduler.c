#include "kernel/process.h"
#include "mm/kmalloc.h"
#include "mm/memory.h"
#include "util/util.h"
#include "arch/x86/gdt.h"
#include "drivers/GUI/framebuffer.h"

process_t *current_process = 0;
process_t *ready_queue = 0;
static int next_pid = 0;

extern void switch_context(uint32_t *old_esp, uint32_t new_esp);
extern void irq_ret_stub();

void scheduler_init(){
    process_t *kernel_proc = (process_t*)kmalloc(sizeof(process_t));
    kernel_proc->pid = 0;
    kernel_proc->page_dir = mem_get_current_page_dir();
    kernel_proc->next = kernel_proc;

    current_process = kernel_proc;
    ready_queue = kernel_proc;
}

void schedule(){
    if (!current_process) return;
    process_t *next_proc = current_process->next;

    if(next_proc == current_process) return;

    set_tss_stack(next_proc->kernel_stack);

    process_t *prev_proc = current_process;
    current_process = next_proc;

    switch_context(&prev_proc->esp, next_proc->esp);
}

void process_create(uint8_t *code, uint32_t size) {
    if (size < sizeof(QuarkExec)) return;

    QuarkExec *exec = (QuarkExec*)code;
    if (exec->magic != 0x5845517F) return; // QEX magic

    process_t *proc = (process_t*)kmalloc(sizeof(process_t));
    proc->pid = next_pid++;

    uint32_t kstack_size = 4096;
    void* kstack_phys = (void*)pmm_alloc_page_frame();
    void* kstack_virt = kmalloc(kstack_size);
    proc->kernel_stack = (uint32_t)kstack_virt + kstack_size;

    uint32_t user_code_virt = 0x400000 + (proc->pid * 0x100000);
    uint32_t user_stack_virt = 0x800000 + (proc->pid * 0x100000);

    uint32_t flags = PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE | PAGE_FLAG_USER;

    uint32_t user_stack_phys = pmm_alloc_page_frame();
    mem_map_page(user_stack_virt, user_stack_phys, flags);
    memset((void*)user_stack_virt, 0, 4096); 

    uint32_t code_payload_size = size - sizeof(QuarkExec);
    
    uint32_t pages_needed = (code_payload_size + 0xFFF) / 4096; 
    pages_needed++; 

    for (uint32_t i = 0; i < pages_needed; i++) {
        uint32_t code_phys = pmm_alloc_page_frame();
        if (!code_phys) {
            kernel_panic("OOM in process_create");
        }
        
        uint32_t target_addr = user_code_virt + (i * 4096);
        mem_map_page(target_addr, code_phys, flags);
        
        memset((void*)target_addr, 0, 4096);
    }

    memcpy((void*)user_code_virt, code + sizeof(QuarkExec), code_payload_size);

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