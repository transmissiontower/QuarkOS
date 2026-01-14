import struct
import sys

def build_qex(binary_path, output_path):
    try:
        with open(binary_path, "rb") as f:
            code_data = f.read()
    except FileNotFoundError:
        print(f"Error: File {binary_path} not found")
        return

    magic = 0x5845517F
    entry_point = 0x400000
    code_size = len(code_data)
    data_size = 0
    bss_size = 4096 

    header = struct.pack("<IIIII", magic, entry_point, code_size, data_size, bss_size)

    with open(output_path, "wb") as f:
        f.write(header)
        f.write(code_data)
    
    print(f"Built {output_path}: Size={code_size} bytes")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python qex_builder.py <input.bin> <output.qex>")
    else:
        build_qex(sys.argv[1], sys.argv[2])