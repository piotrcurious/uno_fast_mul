
import math
import argparse
from hershey_parser import load_font

def get_strokes(initial_strokes):
    if not initial_strokes: return []
    strokes = [list(s) for s in initial_strokes]

    # Smart join strokes that share endpoints
    changed = True
    while changed:
        changed = False
        for i in range(len(strokes)):
            for j in range(i + 1, len(strokes)):
                s1 = strokes[i]
                s2 = strokes[j]
                if not s1 or not s2: continue

                # Check all 4 connection possibilities
                if s1[-1] == s2[0]:
                    s1.extend(s2[1:])
                    strokes[j] = []
                    changed = True; break
                elif s1[-1] == s2[-1]:
                    s1.extend(s2[::-1][1:])
                    strokes[j] = []
                    changed = True; break
                elif s1[0] == s2[-1]:
                    s2.extend(s1[1:])
                    strokes[i] = s2
                    strokes[j] = []
                    changed = True; break
                elif s1[0] == s2[0]:
                    s1 = s2[::-1] + s1[1:]
                    strokes[i] = s1
                    strokes[j] = []
                    changed = True; break
            if changed: break
        strokes = [s for s in strokes if s]

    # Normalize stroke directions for reading order
    normalized = []
    for s in strokes:
        if not s: continue
        dx = s[-1][0] - s[0][0]
        dy = s[-1][1] - s[0][1]
        # Favor left-to-right, then top-to-bottom
        if dx < -1.0: # Right-to-left
            s = s[::-1]
        elif abs(dx) < 1.0 and dy < -1.0: # Vertical, bottom-to-top
            s = s[::-1]
        normalized.append(s)
    return normalized

def get_stroke_length(stroke):
    return sum(math.sqrt((stroke[i+1][0]-stroke[i][0])**2 + (stroke[i+1][1]-stroke[i][1])**2) for i in range(len(stroke)-1))

def sample_stroke(stroke, num_samples):
    total_l = get_stroke_length(stroke)
    if num_samples <= 0: return []

    # 1. First pass: sample positions only
    pts = []
    if num_samples == 1:
        target_ls = [0.5 * total_l]
    else:
        target_ls = [(s_idx / (num_samples - 1)) * total_l for s_idx in range(num_samples)]

    for target in target_ls:
        curr = 0; found = False
        for i in range(len(stroke)-1):
            p1, p2 = stroke[i], stroke[i+1]
            l = math.sqrt((p2[0]-p1[0])**2 + (p2[1]-p1[1])**2)
            if curr <= target <= curr + l + 1e-6:
                t = (target - curr) / l if l > 0 else 0
                pts.append((p1[0] + t*(p2[0]-p1[0]), p1[1] + t*(p2[1]-p1[1])))
                found = True; break
            curr += l
        if not found: pts.append(stroke[-1])

    # 2. Second pass: compute smoothed angles
    samples = []
    for i in range(len(pts)):
        # Use neighbors for a more stable tangent
        if len(pts) > 1:
            if i == 0:
                p1, p2 = pts[0], pts[1]
            elif i == len(pts) - 1:
                p1, p2 = pts[-2], pts[-1]
            else:
                p1, p2 = pts[i-1], pts[i+1]
            angle = math.atan2(p2[1]-p1[1], p2[0]-p1[0])
        else:
            # Fallback for single point: use original stroke direction if possible
            if len(stroke) > 1:
                angle = math.atan2(stroke[1][1]-stroke[0][1], stroke[1][0]-stroke[0][0])
            else:
                angle = 0
        samples.append((pts[i][0], pts[i][1], angle))
    return samples

def generate_paths(font_name="futural"):
    font = load_font(font_name)
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
        raw_strokes = list(font.strokes_for_text(char))
        scaled_strokes = [[(p[0]*2.5, p[1]*2.5) for p in s] for s in raw_strokes]
        if not scaled_strokes:
            x_off += 25
            continue
        min_x = min(p[0] for s in scaled_strokes for p in s)
        max_x = max(p[0] for s in scaled_strokes for p in s)

        # Adjust strokes to start from 0 for this character
        adj_strokes = [[(p[0] - min_x, p[1]) for p in s] for s in scaled_strokes]

        for stroke in get_strokes(adj_strokes):
            final_stroke = [(p[0] + x_off, p[1]) for p in stroke]
            angle = math.atan2(final_stroke[-1][1]-final_stroke[0][1], final_stroke[-1][0]-final_stroke[0][0])
            all_strokes.append({'stroke': final_stroke, 'length': get_stroke_length(final_stroke), 'is_horizontal': abs(math.cos(angle)) > 0.7})
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
            # No more aggressive individual flips; let them follow the stroke.
            # However, we ensure the stroke was normalized to reading order above.
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
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", default="futural", help="Hershey font name or path")
    args = parser.parse_args()
    generate_paths(args.font)
