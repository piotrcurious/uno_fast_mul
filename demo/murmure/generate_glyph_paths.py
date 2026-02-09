
import math
from HersheyFonts import HersheyFonts

def get_strokes(lines):
    if not lines:
        return []
    strokes = []
    current_stroke = []
    for p1, p2 in lines:
        if not current_stroke:
            current_stroke.append(p1)
            current_stroke.append(p2)
        else:
            if p1 == current_stroke[-1]:
                current_stroke.append(p2)
            else:
                strokes.append(current_stroke)
                current_stroke = [p1, p2]
    if current_stroke:
        strokes.append(current_stroke)
    return strokes

def generate_paths():
    fonts = HersheyFonts()
    fonts.load_default_font('futural')

    target_text = "Le fractal est immense"

    # 1. Layout the text to get characters and their strokes
    x_offset = 0
    y_offset = 0
    char_spacing = 60

    char_layouts = []

    for char in target_text:
        if char == ' ':
            x_offset += char_spacing
            continue

        char_lines = list(fonts.lines_for_text(char))
        # Scale up the glyph points
        scaled_lines = []
        for p1, p2 in char_lines:
            scaled_lines.append(((p1[0]*2.0, p1[1]*2.0), (p2[0]*2.0, p2[1]*2.0)))
        char_lines = scaled_lines
        strokes = get_strokes(char_lines)

        min_x = 0
        max_x = 0
        if char_lines:
            all_pts = [p for line in char_lines for p in line]
            min_x = min(p[0] for p in all_pts)
            max_x = max(p[0] for p in all_pts)

        char_width = max_x - min_x

        adjusted_strokes = []
        for stroke in strokes:
            adj_stroke = [(p[0] - min_x + x_offset, p[1] + y_offset) for p in stroke]
            adjusted_strokes.append(adj_stroke)

        char_layouts.append({
            'char': char,
            'width': char_width,
            'strokes': adjusted_strokes,
            'x': x_offset
        })

        x_offset += char_width + 20

    # 2. Collect all strokes
    all_strokes_info = []
    for char_idx, layout in enumerate(char_layouts):
        for stroke_idx, stroke in enumerate(layout['strokes']):
            length = 0
            for i in range(len(stroke)-1):
                p1, p2 = stroke[i], stroke[i+1]
                length += math.sqrt((p2[0]-p1[0])**2 + (p2[1]-p1[1])**2)

            mid_idx = len(stroke) // 2
            p_mid = stroke[mid_idx]

            p_start = stroke[0]
            p_end = stroke[-1]
            angle = math.atan2(p_end[1] - p_start[1], p_end[0] - p_start[0])

            if angle > math.pi/2: angle -= math.pi
            elif angle < -math.pi/2: angle += math.pi

            is_horizontal = abs(math.cos(angle)) > 0.7

            all_strokes_info.append({
                'char': layout['char'],
                'stroke': stroke,
                'length': length,
                'mid': p_mid,
                'angle': angle,
                'is_horizontal': is_horizontal
            })

    # 3. Map 28 verses
    num_verses = 28
    verse_anchors = [None] * num_verses
    used_count = [0] * len(all_strokes_info)

    presage_indices = [0,1,2,3, 8,9,10,11, 16,17,18,19]
    horizontal_strokes = [i for i, s in enumerate(all_strokes_info) if s['is_horizontal']]
    vertical_strokes = [i for i, s in enumerate(all_strokes_info) if not s['is_horizontal']]

    for i, v_idx in enumerate(presage_indices):
        if i < len(horizontal_strokes):
            s_idx = horizontal_strokes[i]
        else:
            s_idx = vertical_strokes[i % len(vertical_strokes)]

        s = all_strokes_info[s_idx]
        offset_y = used_count[s_idx] * 12
        verse_anchors[v_idx] = (s['mid'][0], s['mid'][1] + offset_y, s['angle'])
        used_count[s_idx] += 1

    remaining_verses = [i for i in range(num_verses) if verse_anchors[i] is None]
    for i, v_idx in enumerate(remaining_verses):
        # Pick least used stroke
        s_idx = used_count.index(min(used_count))
        s = all_strokes_info[s_idx]
        offset_y = used_count[s_idx] * 12
        verse_anchors[v_idx] = (s['mid'][0], s['mid'][1] + offset_y, s['angle'])
        used_count[s_idx] += 1

    # Manual adjustments: verse_index -> (x_off, y_off, angle_off, scale_mult)
    MANUAL_ADJUSTMENTS = {
        0: (0, 0, 0, 0.4),  # Adjust first verse scale
        27: (0, 0, 0, 0.5) # Adjust last verse scale
    }
    final_anchors = []
    for i in range(num_verses):
        x, y, a = verse_anchors[i]
        scale = 0.35
        if i in MANUAL_ADJUSTMENTS:
            dx, dy, da, ds = MANUAL_ADJUSTMENTS[i]
            x += dx; y += dy; a += da; scale = ds
        final_anchors.append((x, y, a, scale))

    with open("glyph_paths.h", "w") as f:
        f.write("#ifndef GLYPH_PATHS_H\n#define GLYPH_PATHS_H\n\n#include <stdint.h>\n\n")
        f.write(f"#define NUM_SAMPLES {num_verses}\n")
        f.write("struct PathPoint {\n  float x, y, angle, scale;\n};\n\n")
        f.write("const PathPoint VERSE_POSITIONS[NUM_SAMPLES] PROGMEM = {\n")
        for i, (x, y, a, s) in enumerate(final_anchors):
            f.write(f"  {{ {x:.2f}f, {y:.2f}f, {a:.3f}f, {s:.2f}f }}")
            if i < num_verses - 1: f.write(",")
            f.write("\n")
        f.write("};\n\n")

        all_segments = []
        for s in all_strokes_info:
            for i in range(len(s['stroke'])-1):
                all_segments.append((s['stroke'][i], s['stroke'][i+1]))

        f.write(f"#define NUM_MASTER_SEGMENTS {len(all_segments)}\n")
        f.write("struct Segment {\n  float x1, y1, x2, y2;\n};\n\n")
        f.write("const Segment MASTER_PATH[NUM_MASTER_SEGMENTS] PROGMEM = {\n")
        for i, (p1, p2) in enumerate(all_segments):
            f.write(f"  {{ {p1[0]:.2f}f, {p1[1]:.2f}f, {p2[0]:.2f}f, {p2[1]:.2f}f }}")
            if i < len(all_segments) - 1: f.write(",")
            f.write("\n")
        f.write("};\n\n#endif\n")

if __name__ == "__main__":
    generate_paths()
