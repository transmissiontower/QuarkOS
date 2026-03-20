#include "kernel/elf.h"
#include "mm/memory.h"
#include "util/util.h"

#define PT_LOAD 1

#define USER_MIN_ADDR 0x1000U
#define USER_MAX_ADDR 0xB0000000U
#define PAGE_SIZE     0x1000U

typedef struct __attribute__((packed)) {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
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
} Elf32_Ehdr;

typedef struct __attribute__((packed)) {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

static inline uint32_t align_down(uint32_t value, uint32_t align) {
    return value & ~(align - 1);
}

static inline uint32_t align_up(uint32_t value, uint32_t align) {
    return (value + align - 1) & ~(align - 1);
}

static const Elf32_Ehdr *elf_get_header(const uint8_t *data, uint32_t size) {
    if (!data || size < sizeof(Elf32_Ehdr)) return 0;
    const Elf32_Ehdr *hdr = (const Elf32_Ehdr*)data;
    if (hdr->e_ident[0] != 0x7F || hdr->e_ident[1] != 'E' ||
        hdr->e_ident[2] != 'L' || hdr->e_ident[3] != 'F') return 0;
    if (hdr->e_ident[4] != 1 || hdr->e_ident[5] != 1) return 0;
    if (hdr->e_machine != 3 || hdr->e_phoff == 0) return 0;
    if (hdr->e_phentsize != sizeof(Elf32_Phdr) || hdr->e_phnum == 0) return 0;
    uint64_t ph_end = (uint64_t)hdr->e_phoff + (uint64_t)hdr->e_phentsize * hdr->e_phnum;
    if (ph_end > size) return 0;
    return hdr;
}

int elf_validate(const uint8_t *data, uint32_t size) {
    return elf_get_header(data, size) != 0;
}

static int page_present(uint32_t virt) {
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x3FF;
    uint32_t *pd = REC_PAGEDIR;
    if (!(pd[pd_index] & PAGE_FLAG_PRESENT)) return 0;
    uint32_t *pt = Rec_PAGEDIR(pd_index);
    return (pt[pt_index] & PAGE_FLAG_PRESENT) != 0;
}

static int map_user_pages(uint32_t start, uint32_t end) {
    for (uint32_t addr = start; addr < end; addr += PAGE_SIZE) {
        if (page_present(addr)) continue;
        uint32_t phys = mm_pmm_alloc_frame();
        if (!phys) return -1;
        mm_vmm_map_page(addr, phys, PAGE_FLAG_PRESENT | PAGE_FLAG_WRITE | PAGE_FLAG_USER);
        memset((void*)addr, 0, PAGE_SIZE);
    }
    return 0;
}

int elf_load_image(const uint8_t *data, uint32_t size, exec_image_info_t *info) {
    if (!info) return -1;
    const Elf32_Ehdr *hdr = elf_get_header(data, size);
    if (!hdr) return -1;

    info->entry = hdr->e_entry;
    info->image_base = 0xFFFFFFFF;
    info->image_end = 0;

    int mapped_segments = 0;
    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        uint32_t entry_off = hdr->e_phoff + i * hdr->e_phentsize;
        if (entry_off + sizeof(Elf32_Phdr) > size) return -1;
        const Elf32_Phdr *ph = (const Elf32_Phdr*)(data + entry_off);
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;
        if (ph->p_filesz > ph->p_memsz) return -1;

        uint64_t virt_end64 = (uint64_t)ph->p_vaddr + ph->p_memsz;
        if (virt_end64 > USER_MAX_ADDR) return -1;
        if (ph->p_vaddr < USER_MIN_ADDR) return -1;
        uint32_t seg_start = align_down(ph->p_vaddr, PAGE_SIZE);
        uint32_t seg_end = align_up((uint32_t)virt_end64, PAGE_SIZE);

        if (map_user_pages(seg_start, seg_end) != 0) return -1;

        if (ph->p_filesz) {
            uint64_t file_end = (uint64_t)ph->p_offset + ph->p_filesz;
            if (file_end > size) return -1;
            memcpy((void*)ph->p_vaddr, data + ph->p_offset, ph->p_filesz);
        }
        if (ph->p_memsz > ph->p_filesz) {
            memset((void*)(ph->p_vaddr + ph->p_filesz), 0, ph->p_memsz - ph->p_filesz);
        }

        if (seg_start < info->image_base) info->image_base = seg_start;
        if (seg_end > info->image_end) info->image_end = seg_end;
        mapped_segments++;
    }

    if (!mapped_segments) return -1;
    if (info->entry < info->image_base || info->entry >= info->image_end) return -1;
    return 0;
}
