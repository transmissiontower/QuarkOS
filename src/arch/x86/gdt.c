#include "arch/x86/gdt.h"
#include "util/util.h"

extern void gdt_flush(uint32_t);
extern void tss_flush();

struct gdt_entry_struct gdt_entries[6];
struct gdt_ptr_struct gdt_ptr;
struct tss_entry_struct tss_entry;

void gdt_init() {
    gdt_ptr.limit = (sizeof(struct gdt_entry_struct) * 6) - 1;
    gdt_ptr.base = (uint32_t)&gdt_entries;

    set_gdt_gate(0, 0, 0, 0, 0); // Null segment
    set_gdt_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // Kernel code segment
    set_gdt_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Kernel data segment
    set_gdt_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); // User code segment
    set_gdt_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // User data segment
    write_tss(5, 0x10, 0x0);

    gdt_flush((addr_t)&gdt_ptr);
    tss_flush();
}

void write_tss(uint32_t num, uint16_t ss0, uint32_t esp0) {
    uint32_t base = (uint32_t)&tss_entry;
    uint32_t limit = sizeof(tss_entry) - 1;

    set_gdt_gate(num, base, limit, 0xE9, 0x00);
    memset((void *)&tss_entry, 0, sizeof(tss_entry));

    tss_entry.ss0 = ss0;
    tss_entry.esp0 = esp0;

    tss_entry.cs = 0x1B;
    tss_entry.ss = 0x23;   
    tss_entry.ds = 0x23;
    tss_entry.es = 0x23;
    tss_entry.fs = 0x23;
    tss_entry.gs = 0x23;

    tss_entry.iomap_base = sizeof(tss_entry);
}   

void set_gdt_gate(uint32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {

    gdt_entries[num].base_low = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high = (base >> 24) & 0xFF;

    gdt_entries[num].limit = (limit & 0xFFFF);
    gdt_entries[num].flags = (limit >> 16) & 0x0F;
    gdt_entries[num].flags |= (gran & 0xF0);

    gdt_entries[num].access = access;
}

void set_tss_stack(uint32_t kernel_stack) {
    tss_entry.esp0 = kernel_stack;
}