
import re
from PIL import Image

def verify_bitmaps():
    with open("arduino_tables.h", "r") as f:
        content = f.read()

    width = int(re.search(r"GLYPH_WIDTH (\d+)", content).group(1))
    height = int(re.search(r"GLYPH_HEIGHT (\d+)", content).group(1))
    chars_match = re.search(r'GLYPH_CHAR_LIST\[.*?\] PROGMEM = "(.*?)";', content)
    if not chars_match:
        # try without PROGMEM
        chars_match = re.search(r'GLYPH_CHAR_LIST\[.*?\] = "(.*?)";', content)
    chars = chars_match.group(1)

    bitmaps_match = re.search(r"GLYPH_BITMAPS\[.*?\] PROGMEM = \{(.*?)\};", content, re.DOTALL)
    if not bitmaps_match:
        bitmaps_match = re.search(r"GLYPH_BITMAPS\[.*?\] = \{(.*?)\};", content, re.DOTALL)

    all_vals = re.findall(r"\d+", bitmaps_match.group(1))
    all_vals = [int(v) for v in all_vals]

    num_chars = len(chars)
    full_img = Image.new('L', (width * num_chars, height), 0)

    for i in range(num_chars):
        char_vals = all_vals[i*width : (i+1)*width]
        for x, col in enumerate(char_vals):
            for y in range(height):
                if col & (1 << y):
                    full_img.putpixel((i*width + x, y), 255)

    full_img.save("bitmap_verification.png")
    print(f"Verified {num_chars} bitmaps in bitmap_verification.png")

if __name__ == "__main__":
    verify_bitmaps()
