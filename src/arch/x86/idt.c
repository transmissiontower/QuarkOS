#include "arch/x86/idt.h"
#include "drivers/GUI/framebuffer.h"
#include "lib/stdint.h"
#include "lib/stdlib.h"
#include "util/util.h"
#include "video/vga.h"
#include "kernel/syscalls.h"
#include "mm/memory.h"  

struct idt_entry_struct idt_entries[256];
struct idt_ptr_struct idt_ptr;

extern void idt_flush(uint32_t);
extern void syscall_handler(struct InterruptRegisters *regs);

extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();

static void (*isr_stubs[32])(void)
    = { isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7, isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15, isr16,
          isr17, isr18, isr19, isr20, isr21, isr22, isr23, isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31 };

extern void isr128();
extern void isr177();

extern void irq0();
extern void irq1();
extern void irq2();
extern void irq3();
extern void irq4();
extern void irq5();
extern void irq6();
extern void irq7();
extern void irq8();
extern void irq9();
extern void irq10();
extern void irq11();
extern void irq12();
extern void irq13();
extern void irq14();
extern void irq15();

static void (*irq_stubs[16])(void)
    = { irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7, irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15 };

void idt_init() {
    idt_ptr.limit = sizeof(struct idt_entry_struct) * 256 - 1;
    idt_ptr.base = (uint32_t)&idt_entries;

    memset(&idt_entries, 0, sizeof(struct idt_entry_struct) * 256);

    out_port_B(0x20, 0x11);
    out_port_B(0xA0, 0x11);
    out_port_B(0x21, 0x20);
    out_port_B(0xA1, 0x28);
    out_port_B(0x21, 0x04);
    out_port_B(0xA1, 0x02);
    out_port_B(0x21, 0x01);
    out_port_B(0xA1, 0x01);
    out_port_B(0x21, 0x0);
    out_port_B(0xA1, 0x0);

    for (int i = 0; i < 32; i++) {
        set_idt_gate(i, (uint32_t)isr_stubs[i], 0x08, 0x8E);
    }

    for (int i = 0; i < 16; i++) {
        set_idt_gate(i + 32, (uint32_t)irq_stubs[i], 0x08, 0x8E);
    }

    set_idt_gate(128, (uint32_t)isr128, 0x08, 0xEE);
    set_idt_gate(177, (uint32_t)isr177, 0x08, 0x8E);

    idt_flush((uint32_t)&idt_ptr);
}

void set_idt_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt_entries[num].base_low = base & 0xFFFF;
    idt_entries[num].base_high = (base >> 16) & 0xFFFF;
    idt_entries[num].sel = sel;
    idt_entries[num].always0 = 0;
    idt_entries[num].flags = flags;
}

const char *exception_messages[]
    = { "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint", "Into Detected Overflow", "Out of Bounds",
          "Invalid Opcode", "No Coprocessor", "Double Fault", "Coprocessor Segment Overrun", "Bad TSS",
          "Segment Not Present", "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
          "Coprocessor Fault", "Alignment Fault", "Machine Check", "Reserved", "Reserved", "Reserved", "Reserved",
          "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved" };

void isr_handler(struct InterruptRegisters *regs) {
    if(regs->int_no == 128){
        syscall_handler(regs);
        return;
    }

       if (regs->int_no == 14) {
        uint32_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        
        if (cr2 >= 0xC0000000) {
            uint32_t pd_idx = cr2 >> 22;
            uint32_t *current_pd = (uint32_t*)0xFFFFF000;
            
            if (!(current_pd[pd_idx] & 1) && (initial_page_dir[pd_idx] & 1)) {
                current_pd[pd_idx] = initial_page_dir[pd_idx];
                invalidate(cr2);
                return; 
            }
        }
        clear_screen(COLOR_BLUE);
        string_draw(10, 10, "PAGE FAULT DETECTED!", COLOR_WHITE, COLOR_BLUE);
        
        char buf[16]; 
        
        string_draw(10, 30, "Faulting Address (CR2):", COLOR_WHITE, COLOR_BLUE);
        uint_to_hex(cr2, buf);
        string_draw(210, 30, buf, COLOR_WHITE, COLOR_BLUE);

        string_draw(10, 50, "Error Code:", COLOR_WHITE, COLOR_BLUE);
        uint_to_hex(regs->err_code, buf); 
        string_draw(210, 50, buf, COLOR_WHITE, COLOR_BLUE);
        
        string_draw(10, 70, "EIP:", COLOR_WHITE, COLOR_BLUE);
        uint_to_hex(regs->eip, buf); 
        string_draw(210, 70, buf, COLOR_WHITE, COLOR_BLUE);

        swap_buffers();
        asm volatile("cli; hlt");
    }

    if (regs->int_no < 32) {
        clear_screen(COLOR_RED);
        string_draw(50, 50, exception_messages[regs->int_no], COLOR_WHITE, COLOR_RED);
        string_draw(50, 70, "System Halted!", COLOR_WHITE, COLOR_RED);
        swap_buffers();
        asm volatile("cli");
        for (;;);
    }
}

void *irq_routines[16] = { 0 };

void irq_install_handler(int irq, void (*handler)(struct InterruptRegisters *r)) {
    irq_routines[irq] = handler;
}

void irq_uninstall_handler(int irq) {
    irq_routines[irq] = 0;
}

void irq_handler(struct InterruptRegisters *regs) {
    if (regs->int_no >= 40) {
        out_port_B(0xA0, 0x20);
    }
    out_port_B(0x20, 0x20);

    void (*handler)(struct InterruptRegisters *regs);
    handler = irq_routines[regs->int_no - 32];

    if (handler) {
        handler(regs);
    }
}