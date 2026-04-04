import struct
import os
from PIL import Image, ImageFont, ImageDraw

FONT_PATH = "tools/lucide.ttf"
OUTPUT_BIN = "Asm/boot/icons.bin"
OUTPUT_H = "src/user/lib/icons_def.h"
FONT_SIZE = 18
IMG_WIDTH = 256

ICONS = {
    "ICON_CLOSE": 0xeb15,
    "ICON_UP": 0xe95a,
    "ICON_DOWN": 0xe955,
    "ICON_NOTIFICATION": 0xe933,
    "ICON_SAFETY": 0xeaa5,
    "ICON_MONITOR": 0xea46,
    "ICON_CPU": 0xe999,
    "ICON_FOCUS": 0xead5,
    "ICON_KEYBOARD": 0xeb39,
    "ICON_FULLSCREEN": 0xea94,
    "ICON_SHUTDOWN": 0xea7c,
    "ICON_SEARCH": 0xea98,
    "ICON_TERMINAL": 0xead8,
    "ICON_SUN": 0xEACC
}

def bake_icons():
    if not os.path.exists(FONT_PATH):
        print(f"Error: {FONT_PATH} not found")
        return

    SCALE = 4
    font = ImageFont.truetype(FONT_PATH, FONT_SIZE * SCALE)
    img = Image.new("RGBA", (IMG_WIDTH, 512), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    current_x, current_y = 2, 2
    max_h_row = 0
    metrics = []

    header_content = "#ifndef ICONS_DEF_H\n#define ICONS_DEF_H\n\n"

    for idx, (name, code) in enumerate(ICONS.items()):
        char = chr(code)
        temp_img = Image.new("RGBA", (FONT_SIZE * SCALE * 2, FONT_SIZE * SCALE * 2), (0, 0, 0, 0))
        t_draw = ImageDraw.Draw(temp_img)
        t_draw.text((0, FONT_SIZE * SCALE), char, font=font, fill=(255, 255, 255, 255), anchor="ls")

        bbox = temp_img.getbbox()
        if bbox:
            side = FONT_SIZE * SCALE
            glyph_large = temp_img.crop((
                (bbox[0] + bbox[2]) // 2 - side // 2,
                (bbox[1] + bbox[3]) // 2 - side // 2,
                (bbox[0] + bbox[2]) // 2 + side // 2,
                (bbox[1] + bbox[3]) // 2 + side // 2
            ))

            w, h = FONT_SIZE, FONT_SIZE
            glyph = glyph_large.resize((w, h), Image.Resampling.LANCZOS)

            if current_x + w + 2 >= IMG_WIDTH:
                current_x = 2
                current_y += max_h_row + 2
                max_h_row = 0

            img.paste(glyph, (current_x, current_y))

            metrics.append({
                'x': current_x, 'y': current_y, 'w': w, 'h': h,
                'adv': FONT_SIZE,
                'ox': 0,
                'oy': 0
            })

            header_content += f"#define {name} {idx + 32}\n"

            current_x += w + 2
            max_h_row = max(max_h_row, h)

    header_content += "\n#endif"

    final_h = current_y + max_h_row + 2
    img = img.crop((0, 0, IMG_WIDTH, final_h))

    with open(OUTPUT_BIN, "wb") as f:
        f.write(struct.pack('<I', 0x464F4E54))
        f.write(struct.pack('<I', IMG_WIDTH))
        f.write(struct.pack('<I', final_h))
        f.write(struct.pack('<I', FONT_SIZE))
        f.write(struct.pack('<I', FONT_SIZE))
        f.write(struct.pack('<I', len(metrics)))

        for m in metrics:
            f.write(struct.pack('<HHHHhhh', m['x'], m['y'], m['w'], m['h'], m['adv'], m['ox'], m['oy']))

        for r, g, b, a in list(img.getdata()):
            f.write(struct.pack('<I', (a << 24) | (r << 16) | (g << 8) | b))

    with open(OUTPUT_H, "w") as f:
        f.write(header_content)

if __name__ == "__main__":
    bake_icons()