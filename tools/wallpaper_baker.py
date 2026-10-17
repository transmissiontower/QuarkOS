# SPDX-License-Identifier: MIT

import struct
import sys
from PIL import Image

TARGET_W = 1920
TARGET_H = 1080

def bake(input_path, output_path):
    img = Image.open(input_path).convert("RGBA")
    src_w, src_h = img.size

    scale = max(TARGET_W / src_w, TARGET_H / src_h)
    scaled_w = int(src_w * scale + 0.5)
    scaled_h = int(src_h * scale + 0.5)
    img = img.resize((scaled_w, scaled_h), Image.Resampling.LANCZOS)

    left = (scaled_w - TARGET_W) // 2
    top  = (scaled_h - TARGET_H) // 2
    img = img.crop((left, top, left + TARGET_W, top + TARGET_H))

    with open(output_path, "wb") as f:
        pixels = img.get_flattened_data() if hasattr(img, "get_flattened_data") else img.getdata()
        for r, g, b, a in pixels:
            val = (a << 24) | (r << 16) | (g << 8) | b
            f.write(struct.pack('<I', val))
    print(f"Baked wallpaper: {output_path} ({TARGET_W}x{TARGET_H}, cover crop from {src_w}x{src_h})")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python wallpaper_baker.py <input> <output>")
    else:
        bake(sys.argv[1], sys.argv[2])
