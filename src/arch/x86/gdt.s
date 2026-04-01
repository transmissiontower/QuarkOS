global arch_gdt_flush

arch_gdt_flush:
    MOV eax, [esp+4]
    LGDT [eax]

    MOV eax, 0x10
    MOV ds, ax
    MOV es, ax
    MOV fs, ax
    MOV gs, ax
    MOV ss, ax
    JMP 0x08:.flush
.flush:
    RET

global arch_tss_flush

arch_tss_flush:
    MOV ax, 0x2B
    LTR ax
    RET
