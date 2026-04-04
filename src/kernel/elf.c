#include "kernel/elf.h"
#include "mm/memory.h"
#include "util/util.h"

#define ELF_MAGIC 0x464C457F
#define ELF_PT_LOAD 1

#define PF_X 1
#define PF_W 2
#define PF_R 4

typedef struct {
    uint32_t e_magic;
    uint8_t e_class;
    uint8_t e_data;
    uint8_t e_version;
    uint8_t e_osabi;
    uint8_t e_abiversion;
    uint8_t e_pad[7];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version2;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) elf32_phdr_t;

int elf_validate(const uint8_t *data, uint32_t size)
{
    if (!data || size < sizeof(elf32_ehdr_t))
        return 0;
    const elf32_ehdr_t *hdr = (const elf32_ehdr_t *)data;
    if (hdr->e_magic != ELF_MAGIC)
        return 0;
    if (hdr->e_class != 1 || hdr->e_data != 1)
        return 0;
    if (hdr->e_machine != 3)
        return 0;
    if (hdr->e_phoff == 0 || hdr->e_phentsize != sizeof(elf32_phdr_t))
        return 0;
    if (hdr->e_phnum == 0 || hdr->e_phnum > 64)
        return 0;
    if ((uint64_t)hdr->e_phoff + (uint64_t)hdr->e_phnum * hdr->e_phentsize > size)
        return 0;
    return 1;
}

int elf_load_image(const uint8_t *data, uint32_t size, exec_image_info_t *info)
{
    if (!info || !elf_validate(data, size))
        return -1;

    const elf32_ehdr_t *hdr = (const elf32_ehdr_t *)data;
    const elf32_phdr_t *phdr = (const elf32_phdr_t *)(data + hdr->e_phoff);

    uint32_t image_base = 0xFFFFFFFF;
    uint32_t image_end = 0;

    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        if (phdr[i].p_type != ELF_PT_LOAD)
            continue;
        if (phdr[i].p_offset + phdr[i].p_filesz > size)
            return -1;
        if (phdr[i].p_vaddr < 0x400000 || phdr[i].p_vaddr >= 0xC0000000)
            return -1;
        if (phdr[i].p_vaddr + phdr[i].p_memsz > 0xC0000000)
            return -1;

        uint32_t seg_start = phdr[i].p_vaddr & ~0xFFF;
        uint32_t seg_end = (phdr[i].p_vaddr + phdr[i].p_memsz + 0xFFF) & ~0xFFF;

        uint32_t seg_flags = PAGE_FLAG_PRESENT | PAGE_FLAG_USER;
        if (phdr[i].p_flags & PF_W)
            seg_flags |= PAGE_FLAG_WRITE;

        for (uint32_t addr = seg_start; addr < seg_end; addr += 0x1000) {
            uint32_t pd_idx = addr >> 22;
            uint32_t pt_idx = (addr >> 12) & 0x3FF;
            uint32_t *pd = (uint32_t *)0xFFFFF000;
            uint32_t *pt = (uint32_t *)(0xFFC00000 + (pd_idx << 12));

            int already_mapped =
                (pd[pd_idx] & PAGE_FLAG_PRESENT) && (pt[pt_idx] & PAGE_FLAG_PRESENT);

            if (!already_mapped) {
                uint32_t phys = mm_pmm_alloc_frame();
                if (!phys)
                    return -1;
                mm_vmm_map_page(addr, phys, seg_flags | PAGE_FLAG_WRITE);
                memset((void *)addr, 0, 4096);
            } else {
                if (seg_flags & PAGE_FLAG_WRITE) {
                    pt[pt_idx] |= PAGE_FLAG_WRITE;
                    mm_vmm_tlb_flush_single(addr);
                }
            }
        }

        memcpy((void *)phdr[i].p_vaddr, data + phdr[i].p_offset, phdr[i].p_filesz);
        if (phdr[i].p_memsz > phdr[i].p_filesz) {
            uint32_t diff = phdr[i].p_memsz - phdr[i].p_filesz;
            memset((void *)(phdr[i].p_vaddr + phdr[i].p_filesz), 0, diff);
        }

        if (!(seg_flags & PAGE_FLAG_WRITE)) {
            for (uint32_t addr = seg_start; addr < seg_end; addr += 0x1000) {
                uint32_t pd_idx = addr >> 22;
                uint32_t pt_idx = (addr >> 12) & 0x3FF;
                uint32_t *pt = (uint32_t *)(0xFFC00000 + (pd_idx << 12));
                pt[pt_idx] &= ~PAGE_FLAG_WRITE;
                mm_vmm_tlb_flush_single(addr);
            }
        }

        if (phdr[i].p_vaddr < image_base)
            image_base = phdr[i].p_vaddr;
        uint32_t seg_limit = phdr[i].p_vaddr + phdr[i].p_memsz;
        if (seg_limit > image_end)
            image_end = seg_limit;
    }

    info->entry = hdr->e_entry;
    info->image_base = (image_base == 0xFFFFFFFF) ? 0 : image_base;
    info->image_end = image_end;
    if (info->entry < info->image_base || info->entry >= info->image_end)
        return -1;
    return 0;
}
