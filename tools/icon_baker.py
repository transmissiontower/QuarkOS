import struct
import os
from PIL import Image, ImageFont, ImageDraw
from fontTools import ttLib

FONT_PATH = "tools/lucide.ttf"
OUTPUT_BIN = "Asm/boot/icons.bin"
OUTPUT_H = "src/user/lib/icons_def.h"
FONT_SIZE = 48
IMG_WIDTH = 1024  

def load_icons():
    tt = ttLib.TTFont(FONT_PATH)
    cmap = tt.getBestCmap()  
    icons = {}
    for cp, name in sorted(cmap.items()):
        key = "ICON_" + name.upper().replace("-", "_").replace(".", "_")
        icons[key] = cp
    return icons

def bake_icons():
    if not os.path.exists(FONT_PATH):
        print(f"Error: {FONT_PATH} not found")
        return

    ICONS = load_icons()
    print(f"Loaded {len(ICONS)} icons from {FONT_PATH}")

    SCALE = 4
    font = ImageFont.truetype(FONT_PATH, FONT_SIZE * SCALE)
    cols = IMG_WIDTH // (FONT_SIZE + 2)
    est_rows = (len(ICONS) + cols - 1) // cols
    est_h = (est_rows + 1) * (FONT_SIZE + 2) + 4
    img = Image.new("RGBA", (IMG_WIDTH, est_h), (0, 0, 0, 0))
    current_x, current_y = 2, 2
    max_h_row = 0
    metrics = []
    baked_names = [] 

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
                'oy': 0,
                'cp': code,
            })
            baked_names.append(name)

            current_x += w + 2
            max_h_row = max(max_h_row, h)

    for name, m in zip(baked_names, metrics):
        header_content += f"#define {name} 0x{m['cp']:X}\n"

    header_content += """
/* Semantic aliases */
#define ICON_CLOSE        ICON_UNIEB15
#define ICON_UP           ICON_UNIE95A
#define ICON_DOWN         ICON_UNIE955
#define ICON_NOTIFICATION ICON_UNIE933
#define ICON_SAFETY       ICON_UNIEAA5
#define ICON_MONITOR      ICON_UNIEA46
#define ICON_CPU          ICON_UNIE999
#define ICON_FOCUS        ICON_UNIEAD5
#define ICON_KEYBOARD     ICON_UNIEB39
#define ICON_FULLSCREEN   ICON_UNIEA94
#define ICON_SHUTDOWN     ICON_UNIEA7C
#define ICON_SEARCH       ICON_UNIEA98
#define ICON_TERMINAL     ICON_UNIEAD8
#define ICON_SUN          ICON_UNIEACC
#define ICON_FOLDER       ICON_UNIE9D9
#define ICON_FOLDER_OPEN  ICON_UNIE9D7
#define ICON_FILE_TEXT    ICON_UNIE9C7
#define ICON_ARROW_LEFT   ICON_UNIE91C
#define ICON_ARROW_RIGHT  ICON_UNIE91E
#define ICON_HOME         ICON_UNIEA02
#define ICON_HARD_DRIVE   ICON_UNIE9F8
#define ICON_USERS        ICON_UNIEAFE
#define ICON_CLOCK        ICON_UNIE970
#define ICON_BINARY       ICON_UNIE9BF
"""
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
            f.write(struct.pack('<IHHHHhhh', m['cp'], m['x'], m['y'], m['w'], m['h'], m['adv'], m['ox'], m['oy']))

        for r, g, b, a in list(img.getdata()):
            f.write(struct.pack('<I', (a << 24) | (r << 16) | (g << 8) | b))

    with open(OUTPUT_H, "w") as f:
        f.write(header_content)

if __name__ == "__main__":
    bake_icons()