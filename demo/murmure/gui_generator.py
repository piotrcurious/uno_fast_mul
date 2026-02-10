
import tkinter as tk
from tkinter import messagebox, filedialog
import math
import re
import os
from hershey_parser import load_font

# Helper functions for stroke management (shared with interactive_generator.py)
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

class GUIGenerator:
    def __init__(self, root):
        self.root = root
        self.root.title("Murmure - Graphical Path Generator")

        self.font = None
        self.target_text = ""
        self.all_strokes = []
        self.assignments = [] # List of {'stroke_id': int, 'verse': str}
        self.selected_stroke_id = -1

        self.char_spacing = 80
        self.glyph_scale = 2.5

        self.setup_ui()
        self.load_default_font()

    def setup_ui(self):
        # Top frame
        top_frame = tk.Frame(self.root)
        top_frame.pack(side=tk.TOP, fill=tk.X, padx=5, pady=5)

        tk.Label(top_frame, text="Target Text:").pack(side=tk.LEFT)
        self.text_entry = tk.Entry(top_frame, width=40)
        self.text_entry.pack(side=tk.LEFT, padx=5)
        self.text_entry.bind("<Return>", lambda e: self.update_text())

        tk.Button(top_frame, text="Update", command=self.update_text).pack(side=tk.LEFT, padx=5)
        tk.Button(top_frame, text="Load Font", command=self.browse_font).pack(side=tk.LEFT, padx=5)

        # Main area
        main_frame = tk.Frame(self.root)
        main_frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        # Left: Canvas
        canvas_frame = tk.Frame(main_frame)
        canvas_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.canvas = tk.Canvas(canvas_frame, bg="black", width=800, height=400)
        self.canvas.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        self.canvas.bind("<Button-1>", self.on_canvas_click)

        # Right: Verse assignment
        right_frame = tk.Frame(main_frame, width=300)
        right_frame.pack(side=tk.RIGHT, fill=tk.Y, padx=5)

        tk.Label(right_frame, text="Assigned Verses:").pack(side=tk.TOP)
        self.verse_list = tk.Listbox(right_frame, width=40)
        self.verse_list.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        self.verse_list.bind("<<ListboxSelect>>", self.on_list_select)

        tk.Label(right_frame, text="New Verse Text:").pack(side=tk.TOP, pady=(10, 0))
        self.verse_entry = tk.Entry(right_frame, width=40)
        self.verse_entry.pack(side=tk.TOP, fill=tk.X)
        self.verse_entry.bind("<Return>", lambda e: self.assign_verse())

        tk.Button(right_frame, text="Assign to Selected Stroke", command=self.assign_verse).pack(side=tk.TOP, pady=5)
        tk.Button(right_frame, text="Remove Assignment", command=self.remove_assignment).pack(side=tk.TOP)

        # Bottom frame
        bottom_frame = tk.Frame(self.root)
        bottom_frame.pack(side=tk.BOTTOM, fill=tk.X, padx=5, pady=5)

        tk.Button(bottom_frame, text="Load Project (glyph_paths.h)", command=self.load_project).pack(side=tk.LEFT, padx=5)
        tk.Button(bottom_frame, text="Save Project (glyph_paths.h)", command=self.save_project).pack(side=tk.LEFT, padx=5)

        self.status_label = tk.Label(bottom_frame, text="Ready", bd=1, relief=tk.SUNKEN, anchor=tk.W)
        self.status_label.pack(side=tk.RIGHT, fill=tk.X, expand=True)

    def load_default_font(self):
        try:
            self.font = load_font("futural")
            self.status_label.config(text="Font loaded: futural")
        except:
            self.status_label.config(text="Default font not found.")

    def browse_font(self):
        filename = filedialog.askopenfilename(title="Select Hershey Font (.jhf)", filetypes=[("JHF files", "*.jhf"), ("All files", "*.*")])
        if filename:
            try:
                self.font = load_font(filename)
                self.status_label.config(text=f"Font loaded: {os.path.basename(filename)}")
                if self.target_text:
                    self.update_text()
            except Exception as e:
                messagebox.showerror("Error", f"Could not load font: {e}")

    def update_text(self):
        text = self.text_entry.get()
        if not text: return
        if not self.font:
            messagebox.showwarning("Warning", "Load a font first.")
            return

        self.target_text = text
        self.all_strokes = []
        # Keep assignments that still have valid stroke IDs if possible?
        # Actually, it's safer to clear or reset. Let's just reset for simplicity or re-map.

        x_off = 50
        y_off = 200
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
                adj = [(p[0] - min_x + x_off, p[1] + y_off) for p in stroke]
                self.all_strokes.append({'char': char, 'stroke': adj, 'id': stroke_id})
                stroke_id += 1
            x_off += (max_x - min_x) + 25

        self.draw_strokes()
        self.update_verse_list()

    def draw_strokes(self):
        self.canvas.delete("all")
        for s in self.all_strokes:
            pts = s['stroke']
            color = "blue"
            width = 1

            # Highlight selected
            if s['id'] == self.selected_stroke_id:
                color = "yellow"
                width = 3

            # Check if assigned
            is_assigned = any(a['stroke_id'] == s['id'] for a in self.assignments)
            if is_assigned and s['id'] != self.selected_stroke_id:
                color = "green"
                width = 2

            for i in range(len(pts)-1):
                self.canvas.create_line(pts[i][0], pts[i][1], pts[i+1][0], pts[i+1][1], fill=color, width=width, tags=f"stroke_{s['id']}")

            # ID label
            mid_p = pts[len(pts)//2]
            self.canvas.create_text(mid_p[0], mid_p[1]-15, text=str(s['id']), fill="white", font=("Arial", 8))

    def update_verse_list(self):
        self.verse_list.delete(0, tk.END)
        for a in self.assignments:
            s_id = a['stroke_id']
            char = "?"
            for s in self.all_strokes:
                if s['id'] == s_id:
                    char = s['char']
                    break
            self.verse_list.insert(tk.END, f"ID {s_id} ('{char}'): {a['verse']}")

    def on_canvas_click(self, event):
        x, y = event.x, event.y
        closest_id = -1
        min_dist = 20

        for s in self.all_strokes:
            for i in range(len(s['stroke'])-1):
                p1 = s['stroke'][i]
                p2 = s['stroke'][i+1]
                dist = self.dist_to_segment((x, y), p1, p2)
                if dist < min_dist:
                    min_dist = dist
                    closest_id = s['id']

        if closest_id != -1:
            self.selected_stroke_id = closest_id
            self.draw_strokes()
            self.status_label.config(text=f"Selected Stroke ID: {closest_id}")
            # If already assigned, show text in entry
            for a in self.assignments:
                if a['stroke_id'] == closest_id:
                    self.verse_entry.delete(0, tk.END)
                    self.verse_entry.insert(0, a['verse'])
                    break
        else:
            self.selected_stroke_id = -1
            self.draw_strokes()

    def dist_to_segment(self, p, s1, s2):
        px, py = p
        x1, y1 = s1
        x2, y2 = s2
        dx, dy = x2-x1, y2-y1
        if dx == 0 and dy == 0:
            return math.sqrt((px-x1)**2 + (py-y1)**2)
        t = ((px-x1)*dx + (py-y1)*dy) / (dx*dx + dy*dy)
        t = max(0, min(1, t))
        closest_x = x1 + t*dx
        closest_y = y1 + t*dy
        return math.sqrt((px-closest_x)**2 + (py-closest_y)**2)

    def on_list_select(self, event):
        selection = self.verse_list.curselection()
        if selection:
            a = self.assignments[selection[0]]
            self.selected_stroke_id = a['stroke_id']
            self.verse_entry.delete(0, tk.END)
            self.verse_entry.insert(0, a['verse'])
            self.draw_strokes()

    def assign_verse(self):
        if self.selected_stroke_id == -1:
            messagebox.showwarning("Warning", "Select a stroke first.")
            return
        text = self.verse_entry.get()
        if not text:
            return

        # Update or add
        found = False
        for a in self.assignments:
            if a['stroke_id'] == self.selected_stroke_id:
                a['verse'] = text
                found = True
                break
        if not found:
            self.assignments.append({'stroke_id': self.selected_stroke_id, 'verse': text})

        self.update_verse_list()
        self.draw_strokes()
        self.status_label.config(text=f"Assigned verse to stroke {self.selected_stroke_id}")

    def remove_assignment(self):
        if self.selected_stroke_id == -1: return
        self.assignments = [a for a in self.assignments if a['stroke_id'] != self.selected_stroke_id]
        self.update_verse_list()
        self.draw_strokes()

    def load_project(self):
        filename = filedialog.askopenfilename(title="Open glyph_paths.h", filetypes=[("Header files", "*.h"), ("All files", "*.*")])
        if not filename: return
        try:
            with open(filename, "r") as f:
                content = f.read()
            target_match = re.search(r"// META: TARGET_TEXT: (.*)", content)
            if target_match:
                self.text_entry.delete(0, tk.END)
                self.text_entry.insert(0, target_match.group(1).strip())
                self.update_text()
            else:
                messagebox.showerror("Error", "No metadata found in file.")
                return

            assign_matches = re.findall(r"// META: ASSIGN: (\d+) \| (.*)", content)
            self.assignments = []
            for s_id, verse in assign_matches:
                self.assignments.append({'stroke_id': int(s_id), 'verse': verse})

            self.update_verse_list()
            self.draw_strokes()
            self.status_label.config(text=f"Loaded project: {os.path.basename(filename)}")
        except Exception as e:
            messagebox.showerror("Error", f"Could not load project: {e}")

    def save_project(self):
        if not self.target_text:
            messagebox.showwarning("Warning", "Nothing to save.")
            return
        filename = filedialog.asksaveasfilename(title="Save glyph_paths.h", defaultextension=".h", filetypes=[("Header files", "*.h")])
        if not filename: return

        try:
            all_pts = []; v_off = []; v_len = []; v_texts = []
            # Sort assignments by order of appearance if needed, or just as is
            for idx, a in enumerate(self.assignments):
                s_id = a['stroke_id']
                try:
                    s_info = next(s for s in self.all_strokes if s['id'] == s_id)
                except StopIteration: continue

                txt = a['verse']
                n = len(txt)
                # Sample points (need to remove the y_off we added for drawing)
                y_off = 200
                orig_stroke = [(p[0], p[1]-y_off) for p in s_info['stroke']]
                samples = sample_stroke(orig_stroke, n)

                reused_idx = sum(1 for i in range(idx) if self.assignments[i]['stroke_id'] == s_id)
                total_reused = sum(1 for i in range(len(self.assignments)) if self.assignments[i]['stroke_id'] == s_id)
                y_adj = (reused_idx - 0.5 * (total_reused-1)) * 15

                v_off.append(len(all_pts)); v_len.append(n); v_texts.append(txt)
                for i in range(n):
                    x, y, a_rad = samples[i]
                    if a_rad > math.pi/2: a_rad -= math.pi
                    elif a_rad < -math.pi/2: a_rad += math.pi
                    all_pts.append((x, y + y_adj, a_rad, 0.35))

            with open(filename, "w") as f:
                f.write("// AUTO-GENERATED - INTERACTIVE GUI GENERATOR\n")
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

                # Master path segments (normalized)
                all_segs = []
                for s in self.all_strokes:
                    y_off = 200
                    pts = [(p[0], p[1]-y_off) for p in s['stroke']]
                    for i in range(len(pts)-1):
                        all_segs.append((pts[i], pts[i+1]))

                f.write(f"#define NUM_MASTER_SEGMENTS {len(all_segs)}\n")
                f.write("struct Segment { float x1, y1, x2, y2; };\n")
                f.write("const Segment MASTER_PATH[NUM_MASTER_SEGMENTS] PROGMEM = {\n")
                for i, (p1, p2) in enumerate(all_segs):
                    f.write(f" {{ {p1[0]:.2f}f, {p1[1]:.2f}f, {p2[0]:.2f}f, {p2[1]:.2f}f }}{',' if i < len(all_segs)-1 else ''}\n")
                f.write("};\n#endif\n")

            self.status_label.config(text=f"Project saved to {os.path.basename(filename)}")
            messagebox.showinfo("Success", f"Project saved to {filename}")
        except Exception as e:
            messagebox.showerror("Error", f"Could not save project: {e}")

if __name__ == "__main__":
    root = tk.Tk()
    app = GUIGenerator(root)
    root.mainloop()
