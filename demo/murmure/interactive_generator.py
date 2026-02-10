
import math
import re
import sys
import matplotlib.pyplot as plt
from hershey_parser import load_font

# Helper functions for stroke management
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
    if num_samples <= 0: return []
    if num_samples == 1:
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

class InteractiveGenerator:
    def __init__(self, font_name="futural"):
        self.font = load_font(font_name)
        self.target_text = ""
        self.all_strokes = [] # List of {'char': c, 'stroke': pts, 'id': int}
        self.assignments = [] # List of {'stroke_id': int, 'verse': str}
        self.char_spacing = 80
        self.glyph_scale = 2.5

    def set_target_text(self, text):
        self.target_text = text
        self.all_strokes = []
        self.assignments = []
        x_off = 0
        stroke_id = 0
        for char in text:
            if char == ' ':
                x_off += self.char_spacing
                continue
            lines = list(self.font.lines_for_text(char))
            scaled = [((p1[0]*self.glyph_scale, p1[1]*self.glyph_scale), (p2[0]*self.glyph_scale, p2[1]*self.glyph_scale)) for p1, p2 in lines]
            if not scaled:
                x_off += 20
                continue
            min_x = min(p[0] for line in scaled for p in line)
            max_x = max(p[0] for line in scaled for p in line)
            for stroke in get_strokes(scaled):
                adj = [(p[0] - min_x + x_off, p[1]) for p in stroke]
                self.all_strokes.append({'char': char, 'stroke': adj, 'id': stroke_id})
                stroke_id += 1
            x_off += (max_x - min_x) + 25

    def assign_verse(self, stroke_id, verse_text):
        self.assignments = [a for a in self.assignments if a['stroke_id'] != stroke_id]
        self.assignments.append({'stroke_id': stroke_id, 'verse': verse_text})

    def preview(self, filename="interactive_preview.png"):
        if not self.all_strokes:
            print("No paths to preview.")
            return
        plt.figure(figsize=(15, 5))
        for s in self.all_strokes:
            pts = s['stroke']
            plt.plot([p[0] for p in pts], [-p[1] for p in pts], 'b-', alpha=0.5)
            mid_p = pts[len(pts)//2]
            plt.text(mid_p[0], -mid_p[1], str(s['id']), color='red', fontsize=10, weight='bold')

        for a in self.assignments:
            try:
                s_id = a['stroke_id']
                s = next(s for s in self.all_strokes if s['id'] == s_id)
                pts = s['stroke']
                mid_p = pts[len(pts)//2]
                plt.text(mid_p[0], -mid_p[1] - 10, a['verse'][:15]+"...", color='green', fontsize=8)
            except StopIteration:
                pass

        plt.axis('equal')
        plt.title(f"Target: '{self.target_text}'")
        plt.savefig(filename)
        plt.close()
        print(f"Preview saved to {filename}")

    def save_header(self, filename="glyph_paths.h"):
        all_pts = []; v_off = []; v_len = []; v_texts = []
        for idx, a in enumerate(self.assignments):
            try:
                s_id = a['stroke_id']
                s_info = next(s for s in self.all_strokes if s['id'] == s_id)
                txt = a['verse']
                n = len(txt)
                samples = sample_stroke(s_info['stroke'], n)
                reused_idx = sum(1 for i in range(idx) if self.assignments[i]['stroke_id'] == s_id)
                total_reused = sum(1 for i in range(len(self.assignments)) if self.assignments[i]['stroke_id'] == s_id)
                y_adj = (reused_idx - 0.5 * (total_reused-1)) * 15
                v_off.append(len(all_pts)); v_len.append(n); v_texts.append(txt)
                for i in range(n):
                    x, y, a_rad = samples[i]
                    if a_rad > math.pi/2: a_rad -= math.pi
                    elif a_rad < -math.pi/2: a_rad += math.pi
                    all_pts.append((x, y + y_adj, a_rad, 0.35))
            except StopIteration:
                continue

        with open(filename, "w") as f:
            f.write("// AUTO-GENERATED - INTERACTIVE PATH GENERATOR\n")
            f.write(f"// META: TARGET_TEXT: {self.target_text}\n")
            for a in self.assignments:
                f.write(f"// META: ASSIGN: {a['stroke_id']} | {a['verse']}\n")
            f.write("\n")
            f.write("#ifndef GLYPH_PATHS_H\n#define GLYPH_PATHS_H\n#include <stdint.h>\n#include <Arduino.h>\n")
            f.write("struct PathPoint { float x, y, angle, scale; };\n")
            f.write(f"#define TOTAL_VERSE_CHARS {len(all_pts)}\n")
            f.write("const PathPoint ALL_VERSE_CHARS[TOTAL_VERSE_CHARS] PROGMEM = {\n")
            for i, (x, y, a, s) in enumerate(all_pts):
                f.write(f" {{ {x:.2f}f, {y:.2f}f, {a:.3f}f, {s:.2f}f }}{',' if i < len(all_pts)-1 else ''}\n")
            f.write(f"}};\n#define NUM_VERSES {len(v_texts)}\n")
            f.write(f"const uint16_t VERSE_OFFSETS[NUM_VERSES] PROGMEM = {{ {', '.join(map(str, v_off))} }};\n")
            f.write(f"const uint8_t VERSE_LENGTHS[NUM_VERSES] PROGMEM = {{ {', '.join(map(str, v_len))} }};\n")
            f.write("const char* const VERSES[NUM_VERSES] PROGMEM = {\n")
            for i, txt in enumerate(v_texts):
                escaped = txt.replace('"', '\\"')
                f.write(f"  \"{escaped}\"{',' if i < len(v_texts)-1 else ''}\n")
            f.write("};\n\n")
            all_segs = [(s['stroke'][i], s['stroke'][i+1]) for s in self.all_strokes for i in range(len(s['stroke'])-1)]
            f.write(f"#define NUM_MASTER_SEGMENTS {len(all_segs)}\n")
            f.write("struct Segment { float x1, y1, x2, y2; };\n")
            f.write("const Segment MASTER_PATH[NUM_MASTER_SEGMENTS] PROGMEM = {\n")
            for i, (p1, p2) in enumerate(all_segs):
                f.write(f" {{ {p1[0]:.2f}f, {p1[1]:.2f}f, {p2[0]:.2f}f, {p2[1]:.2f}f }}{',' if i < len(all_segs)-1 else ''}\n")
            f.write("};\n#endif\n")
        print(f"Saved to {filename}")

    def load_header(self, filename="glyph_paths.h"):
        try:
            with open(filename, "r") as f:
                content = f.read()
            target_match = re.search(r"// META: TARGET_TEXT: (.*)", content)
            if target_match:
                self.set_target_text(target_match.group(1).strip())
            assign_matches = re.findall(r"// META: ASSIGN: (\d+) \| (.*)", content)
            self.assignments = []
            for s_id, verse in assign_matches:
                self.assign_verse(int(s_id), verse)
            print(f"Loaded from {filename}")
        except Exception as e:
            print(f"Error loading: {e}")

def main():
    gen = InteractiveGenerator()
    print("Interactive Path Generator. Type 'help' for commands.")
    while True:
        try:
            line = input("> ").strip()
            if not line: continue
            cmd = line.split()[0].lower()
            args = line[len(cmd):].strip()

            if cmd == 'text':
                gen.set_target_text(args)
                print(f"Target text set. Strokes generated: {len(gen.all_strokes)}")
            elif cmd == 'list':
                for s in gen.all_strokes:
                    assigned = [a['verse'] for a in gen.assignments if a['stroke_id'] == s['id']]
                    print(f"ID {s['id']}: Char '{s['char']}' - {assigned if assigned else 'EMPTY'}")
            elif cmd == 'assign':
                match = re.match(r"(\d+)\s+(.*)", args)
                if match:
                    gen.assign_verse(int(match.group(1)), match.group(2))
                    print("Assigned.")
                else:
                    print("Usage: assign <stroke_id> <verse_text>")
            elif cmd == 'preview':
                gen.preview()
            elif cmd == 'font':
                gen.font = load_font(args)
                if gen.target_text:
                    gen.set_target_text(gen.target_text)
                print(f"Font loaded: {args}")
            elif cmd == 'save':
                gen.save_header(args if args else "glyph_paths.h")
            elif cmd == 'load':
                gen.load_header(args if args else "glyph_paths.h")
            elif cmd in ['exit', 'quit']:
                break
            elif cmd == 'help':
                print("Commands: font <name|path>, text <text>, list, assign <id> <verse>, preview, save [file], load [file], exit")
            else:
                print("Unknown command. Type 'help'.")
        except EOFError:
            break
        except Exception as e:
            print(f"Error: {e}")

if __name__ == "__main__":
    main()
