
import os

class HersheyFont:
    def __init__(self, jhf_path):
        self.glyphs = {}
        if not os.path.exists(jhf_path):
            raise FileNotFoundError(f"Font file not found: {jhf_path}")

        with open(jhf_path, 'r') as f:
            lines = f.readlines()

        # Standard ASCII mapping for JHF files often starts at ASCII 32
        for i, line in enumerate(lines):
            ascii_code = 32 + i
            if ascii_code > 126: break # Typically 95-96 chars

            try:
                # Format: Number(5), Count(3), Left(1), Right(1), Coords(2*Count)
                # Some files have 12345 as number, some have actual hershey number.
                num_vertices = int(line[5:8])
                left = ord(line[8]) - ord('R')
                right = ord(line[9]) - ord('R')
                coords_str = line[10:].strip('\r\n')

                vertices = []
                for j in range(0, len(coords_str), 2):
                    pair = coords_str[j:j+2]
                    if len(pair) < 2: break
                    if pair == ' R': # Pen up
                        vertices.append(None)
                    else:
                        x = ord(pair[0]) - ord('R')
                        y = ord(pair[1]) - ord('R')
                        vertices.append((x, y))

                strokes = []
                current_stroke = []
                for v in vertices:
                    if v is None:
                        if current_stroke:
                            strokes.append(current_stroke)
                        current_stroke = []
                    else:
                        current_stroke.append(v)
                if current_stroke:
                    strokes.append(current_stroke)

                self.glyphs[chr(ascii_code)] = {
                    'strokes': strokes,
                    'left': left,
                    'right': right,
                    'width': right - left
                }
            except Exception as e:
                # print(f"Error parsing line {i}: {e}")
                continue

    def lines_for_text(self, text):
        """Returns generator of ((x1, y1), (x2, y2)) for the text.
           Note: This implementation is per-character for compatibility with existing scripts.
        """
        for char in text:
            if char in self.glyphs:
                glyph = self.glyphs[char]
                # To match previous library behavior, we can normalize x so it starts at 0
                # or just return as is if the scripts handle centering.
                # Scripts used: adj = [(p[0] - min_x + x_off, p[1]) for p in stroke]
                # So they handle normalization themselves.
                for stroke in glyph['strokes']:
                    for i in range(len(stroke)-1):
                        yield (stroke[i], stroke[i+1])

def load_font(name_or_path):
    # Try system path first
    sys_path = f"/usr/share/hershey-fonts/{name_or_path}.jhf"
    if os.path.exists(sys_path):
        return HersheyFont(sys_path)
    # Try direct path
    if os.path.exists(name_or_path):
        return HersheyFont(name_or_path)
    # Try with .jhf extension
    if os.path.exists(name_or_path + ".jhf"):
        return HersheyFont(name_or_path + ".jhf")

    raise FileNotFoundError(f"Could not find font: {name_or_path}")

if __name__ == "__main__":
    # Quick test
    try:
        font = load_font("futural")
        print("Loaded futural")
        lines = list(font.lines_for_text("L"))
        print(f"L lines: {lines}")
    except Exception as e:
        print(e)
