# SPDX-License-Identifier: MIT

from PIL import Image, ImageDraw, ImageFont
import struct
import sys
import os

FONT_FILENAME = "tools/Inter-VariableFont_opsz,wght.ttf"
FONT_SIZE = 14
OUTPUT_FILE = "build/font_atlas.bin"
IMG_WIDTH = 512

def _build_chars():
    ranges = [
        (0x0020, 0x007F),
        (0x00A0, 0x0100),
        (0x0100, 0x0180),
        (0x0180, 0x0250),
        (0x0370, 0x0400),
        (0x0400, 0x0500),
        (0x2000, 0x2070),
        (0x20A0, 0x20D0),
        (0x2100, 0x2150),
        (0x2190, 0x2200),
        (0x2200, 0x2300),
        (0x2300, 0x2400),
        (0x2500, 0x2580),
        (0x2580, 0x25A0),
        (0x25A0, 0x2600),
        (0x2600, 0x2700),
        (0x2700, 0x27C0),
    ]
    chars = []
    seen = set()
    for start, end in ranges:
        for cp in range(start, end):
            if cp not in seen:
                seen.add(cp)
                chars.append(chr(cp))
    return chars

CHARS = _build_chars()

def bake():
    if not os.path.exists(FONT_FILENAME):
        print(f"[ERROR] {FONT_FILENAME} not found!")
        sys.exit(1)

    font = ImageFont.truetype(FONT_FILENAME, FONT_SIZE)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    print(f"Font loaded. Size: {FONT_SIZE}, Line Height: {line_height}")

    img = Image.new("RGBA", (IMG_WIDTH, 4096), (0, 0, 0, 0))

    current_x = 2
    current_y = 2
    max_h_row = 0

    metrics = []

    for char in sorted(CHARS, key=ord):
        temp_img = Image.new("RGBA", (100, 100), (0, 0, 0, 0))
        temp_draw = ImageDraw.Draw(temp_img)

        baseline_y = 80
        temp_draw.text((0, baseline_y), char, font=font, fill=(255, 255, 255, 255), anchor="ls")

        bbox = temp_img.getbbox()

        if bbox:
            glyph = temp_img.crop(bbox)
            w, h = glyph.size
            offset_x = bbox[0]
            offset_y = bbox[1] - baseline_y
        else:
            glyph = None
            w = 0
            h = 0
            offset_x = 0
            offset_y = 0

        if current_x + w + 2 >= IMG_WIDTH:
            current_x = 2
            current_y += max_h_row + 2
            max_h_row = 0

        if glyph:
            img.paste(glyph, (current_x, current_y))

        advance = int(font.getlength(char))

        metrics.append({
            'cp': ord(char),
            'x': current_x,
            'y': current_y,
            'w': w,
            'h': h,
            'adv': advance,
            'off_x': offset_x,
            'off_y': offset_y
        })

        if h > max_h_row: max_h_row = h
        current_x += w + 2

    final_height = current_y + max_h_row + 2
    if final_height % 4 != 0: final_height += (4 - (final_height % 4))

    img = img.crop((0, 0, IMG_WIDTH, final_height))

    with open(OUTPUT_FILE, "wb") as f:
        f.write(struct.pack('<I', 0x464F4E54))
        f.write(struct.pack('<I', IMG_WIDTH))
        f.write(struct.pack('<I', final_height))
        f.write(struct.pack('<I', line_height))
        f.write(struct.pack('<I', ascent))
        f.write(struct.pack('<I', len(metrics)))

        for m in metrics:
            f.write(struct.pack('<IHHHHhhh',
                m['cp'],
                m['x'], m['y'], m['w'], m['h'],
                m['adv'], m['off_x'], m['off_y']))

        pixels = img.get_flattened_data() if hasattr(img, "get_flattened_data") else img.getdata()
        for r, g, b, a in pixels:
            val = (a << 24) | (r << 16) | (g << 8) | b
            f.write(struct.pack('<I', val))

    print(f"SUCCESS! Baked {len(metrics)} chars.")

if __name__ == "__main__":
    bake()
