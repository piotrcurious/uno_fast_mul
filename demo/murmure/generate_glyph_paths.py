
import math
from HersheyFonts import HersheyFonts

def get_strokes(lines):
    if not lines: return []
    strokes = []
    current_stroke = []
    for p1, p2 in lines:
        if not current_stroke:
            current_stroke.append(p1); current_stroke.append(p2)
        else:
            if p1 == current_stroke[-1]: current_stroke.append(p2)
            else:
                strokes.append(current_stroke)
                current_stroke = [p1, p2]
    if current_stroke: strokes.append(current_stroke)
    return strokes

def get_stroke_length(stroke):
    return sum(math.sqrt((stroke[i+1][0]-stroke[i][0])**2 + (stroke[i+1][1]-stroke[i][1])**2) for i in range(len(stroke)-1))

def sample_stroke(stroke, num_samples):
    total_l = get_stroke_length(stroke)
    if num_samples <= 1:
        mid = 0.5 * total_l
        curr = 0
        for i in range(len(stroke)-1):
            p1, p2 = stroke[i], stroke[i+1]
            l = math.sqrt((p2[0]-p1[0])**2 + (p2[1]-p1[1])**2)
            if curr <= mid <= curr + l:
                t = (mid - curr) / l if l > 0 else 0
                return [(p1[0] + t*(p2[0]-p1[0]), p1[1] + t*(p2[1]-p1[1]), math.atan2(p2[1]-p1[1], p2[0]-p1[0]))]
            curr += l
        return [(stroke[0][0], stroke[0][1], 0)]
    samples = []
    for s_idx in range(num_samples):
        target = (s_idx / (num_samples - 1)) * total_l
        curr = 0; found = False
        for i in range(len(stroke)-1):
            p1, p2 = stroke[i], stroke[i+1]
            l = math.sqrt((p2[0]-p1[0])**2 + (p2[1]-p1[1])**2)
            if curr <= target <= curr + l:
                t = (target - curr) / l if l > 0 else 0
                samples.append((p1[0] + t*(p2[0]-p1[0]), p1[1] + t*(p2[1]-p1[1]), math.atan2(p2[1]-p1[1], p2[0]-p1[0])))
                found = True; break
            curr += l
        if not found: samples.append((stroke[-1][0], stroke[-1][1], 0))
    return samples

def generate_paths():
    fonts = HersheyFonts()
    fonts.load_default_font('futural')
    target_text = "Le fractal est immense"
    verses = [
        "L'hiver hesite, presage.", "L'oiseau se tait, presage.",
        "L'eau moins fraiche, presage.", "L'echo s'eloigne, presage.",
        "Murmure, petit murmure.", "Fractal de la rupture.",
        "Repete le trouble, repete.", "Le monde change en cachette.",
        "Fleur en decembre, presage.", "Pluie en ete, presage.",
        "Vent sans saison, presage.", "Ciel incertain, presage.",
        "Murmure, petit murmure.", "Fractal de la rupture.",
        "Repete le trouble, repete.", "Le monde change en cachette.",
        "Rats s'enfuient, presage.", "Mensonge use, presage.",
        "Foi qui baisse, presage.", "Pouvoir las, presage.",
        "Murmure, petit murmure.", "Fractal de la rupture.",
        "Repete le trouble, repete.", "Le monde change en cachette.",
        "Chaque nuance, meme instance.", "Le grand schema s'avance.",
        "Ecoute bien, enfin sens.", "Le fractal est immense."
    ]

    x_off = 0; char_spacing = 80; all_strokes = []
    for char in target_text:
        if char == ' ': x_off += char_spacing; continue
        lines = list(fonts.lines_for_text(char))
        scaled = [((p1[0]*2.5, p1[1]*2.5), (p2[0]*2.5, p2[1]*2.5)) for p1, p2 in lines]
        min_x = min(p[0] for line in scaled for p in line) if scaled else 0
        max_x = max(p[0] for line in scaled for p in line) if scaled else 0
        for stroke in get_strokes(scaled):
            adj = [(p[0] - min_x + x_off, p[1]) for p in stroke]
            angle = math.atan2(adj[-1][1]-adj[0][1], adj[-1][0]-adj[0][0])
            all_strokes.append({'stroke': adj, 'length': get_stroke_length(adj), 'is_horizontal': abs(math.cos(angle)) > 0.7})
        x_off += (max_x - min_x) + 25

    num_v = 28; stroke_assign = [None]*num_v; used_c = [0]*len(all_strokes)
    presage = [0,1,2,3, 8,9,10,11, 16,17,18,19]
    horiz = [i for i, s in enumerate(all_strokes) if s['is_horizontal']]
    vert = [i for i, s in enumerate(all_strokes) if not s['is_horizontal']]
    for i, v_idx in enumerate(presage):
        s_idx = horiz[i] if i < len(horiz) else vert[i % len(vert)]
        stroke_assign[v_idx] = s_idx; used_c[s_idx] += 1
    for i in range(num_v):
        if stroke_assign[i] is None:
            s_idx = used_c.index(min(used_c))
            stroke_assign[i] = s_idx; used_c[s_idx] += 1

    all_pts = []; v_off = []; v_len = []; v_texts = []
    for v_idx in range(num_v):
        s_idx = stroke_assign[v_idx]; s_info = all_strokes[s_idx]; txt = verses[v_idx]; n = len(txt)
        samples = sample_stroke(s_info['stroke'], n)
        reused = sum(1 for prev in range(v_idx) if stroke_assign[prev] == s_idx)
        y_adj = (reused - 0.5 * (used_c[s_idx]-1)) * 15
        v_off.append(len(all_pts)); v_len.append(n); v_texts.append(txt)
        for i in range(n):
            x, y, a = samples[i]
            if a > math.pi/2: a -= math.pi
            elif a < -math.pi/2: a += math.pi
            all_pts.append((x, y + y_adj, a, 0.4 if v_idx in [0, 27] else 0.35))

    with open("glyph_paths.h", "w") as f:
        f.write("// AUTO-GENERATED - NON-INTERACTIVE\n")
        f.write(f"// META: TARGET_TEXT: {target_text}\n")
        for i, v in enumerate(stroke_assign):
            f.write(f"// META: ASSIGN: {v} | {verses[i]}\n")
        f.write("\n")
        f.write("#ifndef GLYPH_PATHS_H\n#define GLYPH_PATHS_H\n#include <stdint.h>\n#include <Arduino.h>\n")
        f.write("struct PathPoint { float x, y, angle, scale; };\n")
        f.write(f"#define TOTAL_VERSE_CHARS {len(all_pts)}\n")
        f.write("const PathPoint ALL_VERSE_CHARS[TOTAL_VERSE_CHARS] PROGMEM = {\n")
        for i, (x, y, a, s) in enumerate(all_pts):
            f.write(f" {{ {x:.2f}f, {y:.2f}f, {a:.3f}f, {s:.2f}f }}{',' if i < len(all_pts)-1 else ''}\n")
        f.write(f"}};\n#define NUM_VERSES {num_v}\n")
        f.write(f"const uint16_t VERSE_OFFSETS[NUM_VERSES] PROGMEM = {{ {', '.join(map(str, v_off))} }};\n")
        f.write(f"const uint8_t VERSE_LENGTHS[NUM_VERSES] PROGMEM = {{ {', '.join(map(str, v_len))} }};\n")
        f.write("const char* const VERSES[NUM_VERSES] PROGMEM = {\n")
        for i, txt in enumerate(v_texts):
            f.write(f"  \"{txt}\"{',' if i < len(v_texts)-1 else ''}\n")
        f.write("};\n")
        all_segs = [(s['stroke'][i], s['stroke'][i+1]) for s in all_strokes for i in range(len(s['stroke'])-1)]
        f.write(f"#define NUM_MASTER_SEGMENTS {len(all_segs)}\n")
        f.write("struct Segment { float x1, y1, x2, y2; };\n")
        f.write("const Segment MASTER_PATH[NUM_MASTER_SEGMENTS] PROGMEM = {\n")
        for i, (p1, p2) in enumerate(all_segs):
            f.write(f" {{ {p1[0]:.2f}f, {p1[1]:.2f}f, {p2[0]:.2f}f, {p2[1]:.2f}f }}{',' if i < len(all_segs)-1 else ''}\n")
        f.write("};\n#endif\n")

if __name__ == "__main__":
    generate_paths()
