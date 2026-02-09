
import math
from HersheyFonts import HersheyFonts

def generate_paths():
    fonts = HersheyFonts()
    fonts.load_default_font('futural')

    target_text = "Le fractal est immense"

    # 1. Layout the text to get a master path
    x_offset = 0
    y_offset = 0
    scale = 1.0
    char_spacing = 20

    master_segments = []

    for char in target_text:
        if char == ' ':
            x_offset += char_spacing
            continue

        # Get lines for this char
        # lines_for_text returns lines relative to (0,0)
        char_lines = list(fonts.lines_for_text(char))

        # Determine width of char (approx)
        min_x = 0
        max_x = 0
        if char_lines:
            all_pts = [p for line in char_lines for p in line]
            min_x = min(p[0] for p in all_pts)
            max_x = max(p[0] for p in all_pts)

        char_width = max_x - min_x

        # Offset and add to master
        for line in char_lines:
            p1, p2 = line
            np1 = (p1[0] - min_x + x_offset, p1[1] + y_offset)
            np2 = (p2[0] - min_x + x_offset, p2[1] + y_offset)
            master_segments.append((np1, np2))

        x_offset += char_width + 5 # some gap

    # 2. Calculate total length and sample 28 points
    total_length = 0
    segment_lengths = []
    for p1, p2 in master_segments:
        l = math.sqrt((p2[0]-p1[0])**2 + (p2[1]-p1[1])**2)
        segment_lengths.append(l)
        total_length += l

    num_samples = 28
    sample_positions = []

    if total_length > 0:
        for i in range(num_samples):
            target_d = (i / (num_samples - 1)) * total_length if num_samples > 1 else 0

            # Find which segment this distance falls into
            curr_d = 0
            found = False
            for j, l in enumerate(segment_lengths):
                if curr_d <= target_d <= curr_d + l:
                    # Interpolate
                    t = (target_d - curr_d) / l if l > 0 else 0
                    p1, p2 = master_segments[j]
                    x = p1[0] + t * (p2[0] - p1[0])
                    y = p1[1] + t * (p2[1] - p1[1])

                    # Angle of segment
                    angle = math.atan2(p2[1]-p1[1], p2[0]-p1[0])

                    sample_positions.append((x, y, angle))
                    found = True
                    break
                curr_d += l
            if not found and master_segments:
                # Last point
                p1, p2 = master_segments[-1]
                angle = math.atan2(p2[1]-p1[1], p2[0]-p1[0])
                sample_positions.append((p2[0], p2[1], angle))
    else:
        sample_positions = [(0,0,0)] * num_samples

    # 3. Output to C header
    with open("glyph_paths.h", "w") as f:
        f.write("#ifndef GLYPH_PATHS_H\n")
        f.write("#define GLYPH_PATHS_H\n\n")
        f.write("#include <stdint.h>\n\n")

        f.write(f"#define NUM_SAMPLES {num_samples}\n")
        f.write("struct PathPoint {\n  float x, y, angle;\n};\n\n")

        f.write("const PathPoint VERSE_POSITIONS[NUM_SAMPLES] PROGMEM = {\n")
        for i, (x, y, a) in enumerate(sample_positions):
            f.write(f"  {{ {x:.2f}f, {y:.2f}f, {a:.3f}f }}")
            if i < num_samples - 1: f.write(",")
            f.write("\n")
        f.write("};\n\n")

        f.write(f"#define NUM_MASTER_SEGMENTS {len(master_segments)}\n")
        f.write("struct Segment {\n  float x1, y1, x2, y2;\n};\n\n")
        f.write("const Segment MASTER_PATH[NUM_MASTER_SEGMENTS] PROGMEM = {\n")
        for i, (p1, p2) in enumerate(master_segments):
            f.write(f"  {{ {p1[0]:.2f}f, {p1[1]:.2f}f, {p2[0]:.2f}f, {p2[1]:.2f}f }}")
            if i < len(master_segments) - 1: f.write(",")
            f.write("\n")
        f.write("};\n\n")

        f.write("#endif\n")

    print(f"Generated glyph_paths.h with {len(master_segments)} segments and {num_samples} verse positions.")

if __name__ == "__main__":
    generate_paths()
