#include "lib/stdint.h"

void fpu_init() {
    uint32_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 0x200;
    cr4 |= 0x400;
    asm volatile("mov %0, %%cr4" ::"r"(cr4));

    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2);
    cr0 |= (1 << 1);
    asm volatile("mov %0, %%cr0" ::"r"(cr0));
    asm volatile("fninit");
}