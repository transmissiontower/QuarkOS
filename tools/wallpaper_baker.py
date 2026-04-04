import struct
import sys
from PIL import Image

def bake(input_path, output_path):
    img = Image.open(input_path).convert("RGBA")
    img = img.resize((1920, 1080), Image.Resampling.LANCZOS)

    with open(output_path, "wb") as f:
        pixels = list(img.getdata())
        for r, g, b, a in pixels:
            val = (a << 24) | (r << 16) | (g << 8) | b
            f.write(struct.pack('<I', val))
    print(f"Baked wallpaper: {output_path} (1920x1080)")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python wallpaper_baker.py <input> <output>")
    else:
        bake(sys.argv[1], sys.argv[2])