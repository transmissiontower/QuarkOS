import os
import struct
import sys

SHT_NOBITS = 8
SHF_ALLOC = 0x2


def find_matching_elf(binary_path):
    base, ext = os.path.splitext(binary_path)
    candidates = []
    if base:
        candidates.append(base + ".elf")
    candidates.append(binary_path + ".elf")

    for candidate in candidates:
        if os.path.isfile(candidate):
            return candidate
    return None


def read_elf_metadata(elf_path):
    if not elf_path or not os.path.isfile(elf_path):
        return None

    with open(elf_path, "rb") as f:
        data = f.read()

    if len(data) < 52 or data[:4] != b"\x7fELF":
        return None

    if data[4] != 1 or data[5] != 1:
        return None

    e_entry = struct.unpack_from("<L", data, 24)[0]
    e_phoff = struct.unpack_from("<L", data, 28)[0]
    e_phentsize = struct.unpack_from("<H", data, 42)[0]
    e_phnum = struct.unpack_from("<H", data, 44)[0]

    max_vma = 0
    for i in range(e_phnum):
        offset = e_phoff + i * e_phentsize
        ph = struct.unpack_from("<LLLLLLLL", data, offset)
        p_type = ph[0]
        p_vaddr = ph[3]
        p_memsz = ph[5]
        if p_type == 1:
            if p_vaddr + p_memsz > max_vma:
                max_vma = p_vaddr + p_memsz

    return (e_entry, max_vma)


def build_qex(binary_path, output_path):
    try:
        with open(binary_path, "rb") as f:
            code_data = f.read()
    except FileNotFoundError:
        print(f"Error: File {binary_path} not found")
        return

    magic = 0x5845517F
    code_size = len(code_data)
    data_size = 0

    entry_point = 0x400000
    bss_size = 8 * 1024 * 1024

    elf_path = find_matching_elf(binary_path)
    elf_meta = read_elf_metadata(elf_path) if elf_path else None
    if elf_meta:
        entry_point, max_vma = elf_meta
        if max_vma > 0x400000:
            total_range = max_vma - 0x400000
            bss_size = total_range - code_size
            if bss_size < 0:
                bss_size = 0

    header = struct.pack("<IIIII", magic, entry_point, code_size, data_size, bss_size)

    with open(output_path, "wb") as f:
        f.write(header)
        f.write(code_data)

    print(f"Built {output_path}: Size={code_size} bytes, BSS={bss_size} bytes")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python qex_builder.py <input.bin> <output.qex>")
    else:
        build_qex(sys.argv[1], sys.argv[2])
