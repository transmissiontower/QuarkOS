from PIL import Image, ImageDraw, ImageFont
import struct
import sys
import os

FONT_FILENAME = "tools/Inter-VariableFont_opsz,wght.ttf" 
FONT_SIZE = 14
OUTPUT_FILE = "Asm/boot/font_atlas.bin"
IMG_WIDTH = 512 

CHARS = "".join([chr(i) for i in range(32, 127)]) 

def bake():
    if not os.path.exists(FONT_FILENAME):
        print(f"[ERROR] {FONT_FILENAME} not found!")
        sys.exit(1)

    font = ImageFont.truetype(FONT_FILENAME, FONT_SIZE)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    print(f"Font loaded. Size: {FONT_SIZE}, Line Height: {line_height}")

    img = Image.new("RGBA", (IMG_WIDTH, 1024), (0, 0, 0, 0))
    
    current_x = 2
    current_y = 2
    max_h_row = 0
    
    metrics = [] 

    for char in CHARS:
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
            f.write(struct.pack('<HHHHhhh', 
                m['x'], m['y'], m['w'], m['h'], 
                m['adv'], m['off_x'], m['off_y']))

        pixels = list(img.getdata())
        for r, g, b, a in pixels:
            val = (a << 24) | (r << 16) | (g << 8) | b
            f.write(struct.pack('<I', val))

    print(f"SUCCESS! Baked {len(metrics)} chars.")

if __name__ == "__main__":
    bake()