#!/usr/bin/env python3
"""Write a single active FAT32-LBA partition entry to a blank disk image."""

import os
import struct
import sys


def main():
    image_path = sys.argv[1]
    start_lba = int(sys.argv[2])
    sectors = os.path.getsize(image_path) // 512
    partition_sectors = sectors - start_lba
    if start_lba < 1 or partition_sectors < 1:
        raise SystemExit("invalid MBR partition bounds")

    mbr = bytearray(512)
    struct.pack_into(
        "<B3sB3sII",
        mbr,
        446,
        0x80,
        b"\xfe\xff\xff",
        0x0C,
        b"\xfe\xff\xff",
        start_lba,
        partition_sectors,
    )
    mbr[510:512] = b"\x55\xaa"

    with open(image_path, "r+b") as image:
        image.write(mbr)


if __name__ == "__main__":
    main()
