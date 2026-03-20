#include "arch/x86/gdt.h"
#include "util/util.h"

extern void arch_gdt_flush(uint32_t);
extern void arch_tss_flush();

struct gdt_entry_struct gdt_entries[6];
struct gdt_ptr_struct gdt_ptr;
struct tss_entry_struct tss_entry;

void arch_gdt_init() {
    gdt_ptr.limit = (sizeof(struct gdt_entry_struct) * 6) - 1;
    gdt_ptr.base = (uint32_t)&gdt_entries;

    arch_gdt_set_gate(0, 0, 0, 0, 0);
    arch_gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    arch_gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    arch_gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    arch_gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);
    arch_tss_init(5, 0x10, 0x0);

    arch_gdt_flush((uint32_t)&gdt_ptr);
    arch_tss_flush();
}

void arch_gdt_set_gate(uint32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high = (base >> 24) & 0xFF;
    gdt_entries[num].limit = (limit & 0xFFFF);
    gdt_entries[num].flags = (limit >> 16) & 0x0F;
    gdt_entries[num].flags |= (gran & 0xF0);
    gdt_entries[num].access = access;
}

void arch_tss_init(uint32_t idx, uint16_t ss0, uint32_t esp0) {
    uint32_t base = (uint32_t)&tss_entry;
    arch_gdt_set_gate(idx, base, sizeof(tss_entry) - 1, 0xE9, 0x00);

    memset(&tss_entry, 0, sizeof(tss_entry));
    tss_entry.ss0 = ss0;
    tss_entry.esp0 = esp0;
    tss_entry.cs = 0x1B;
    tss_entry.ss = tss_entry.ds = tss_entry.es = tss_entry.fs = tss_entry.gs = 0x23;
}

void arch_tss_set_stack(uint32_t kernel_stack) {
    tss_entry.esp0 = kernel_stack;
}
