
import math
from HersheyFonts import HersheyFonts
from PIL import Image, ImageDraw

def generate():
    fonts = HersheyFonts()
    fonts.load_default_font('futural')

    chars = " !\"',.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    escaped_chars = chars.replace('"', '\\"').replace("'", "\\'")
    glyph_w = 16
    glyph_h = 24 # Hershey needs a bit more height

    # Tables for fast math (msb, log2, exp2) - can be copied from existing generator or just hardcoded/called
    # Actually, I should probably reuse generator/generate_tables.py if possible, but it doesn't support Hershey.

    # Let's just generate the glyphs and basic tables.

    glyphs = []
    for ch in chars:
        # Create a small image and draw the hershey lines on it
        img = Image.new('1', (glyph_w, glyph_h), 0)
        draw = ImageDraw.Draw(img)

        lines = list(fonts.lines_for_text(ch))
        # Center them. Hershey y goes from -12 to 9 approx.
        # Let's map y=-12 to row 2, y=9 to row 20?
        # x varies.

        all_pts = [p for line in lines for p in line]
        if all_pts:
            min_x = min(p[0] for p in all_pts)
            max_x = max(p[0] for p in all_pts)
            w = max_x - min_x
            x_off = (glyph_w - w) / 2 - min_x
        else:
            x_off = 0

        y_off = glyph_h / 2

        for p1, p2 in lines:
            draw.line([(p1[0] + x_off, p1[1] + y_off), (p2[0] + x_off, p2[1] + y_off)], fill=1)

        # Convert to bit columns
        cols = []
        for x in range(glyph_w):
            col = 0
            for y in range(glyph_h):
                if img.getpixel((x, y)):
                    col |= (1 << y)
            cols.append(col)
        glyphs.append(cols)

    # Output to arduino_tables.h
    with open("arduino_tables.h", "w") as f:
        f.write("#ifndef ARDUINO_TABLES_H\n")
        f.write("#define ARDUINO_TABLES_H\n\n")
        f.write("#include <stdint.h>\n")
        f.write("#ifdef ARDUINO\n#include <avr/pgmspace.h>\n#else\n#define PROGMEM\n#endif\n\n")

        # MSB table
        f.write("const uint8_t msb_table[256] PROGMEM = {\n  ")
        msb = [0 if i == 0 else int(math.floor(math.log2(i))) for i in range(256)]
        f.write(", ".join(map(str, msb)))
        f.write("\n};\n\n")

        # Log2 table
        f.write("const uint16_t log2_table_q8[256] PROGMEM = {\n  ")
        log2 = [round(math.log2(i) * 256) if i >= 1 else 0 for i in range(256)]
        f.write(", ".join(map(str, log2)))
        f.write("\n};\n\n")

        # Exp2 table
        f.write("const uint16_t exp2_table_q8[256] PROGMEM = {\n  ")
        exp2 = [round((2 ** (f / 256)) * 256) for f in range(256)]
        f.write(", ".join(map(str, exp2)))
        f.write("\n};\n\n")

        # Glyphs
        f.write(f"#define GLYPH_WIDTH {glyph_w}\n")
        f.write(f"#define GLYPH_HEIGHT {glyph_h}\n")
        f.write(f"#define GLYPH_COUNT {len(chars)}\n")
        f.write(f'const char GLYPH_CHAR_LIST[{len(chars)+1}] PROGMEM = "{escaped_chars}";\n\n')

        glyph_type = "uint32_t" if glyph_h > 16 else "uint16_t"
        f.write(f"const {glyph_type} GLYPH_BITMAPS[{len(chars) * glyph_w}] PROGMEM = {{\n")
        for i, cols in enumerate(glyphs):
            f.write("  " + ", ".join(map(str, cols)))
            if i < len(glyphs) - 1: f.write(",")
            f.write(f" // '{chars[i]}'\n")
        f.write("};\n\n")

        f.write("#endif\n")

    print(f"Generated arduino_tables.h with {len(chars)} glyphs and math tables.")

if __name__ == "__main__":
    generate()
