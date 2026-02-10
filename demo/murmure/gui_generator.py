
import tkinter as tk
from tkinter import messagebox, filedialog
import math
import re
import os
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

    # Normalize stroke directions for reading order
    normalized = []
    for s in strokes:
        if not s: continue
        dx = s[-1][0] - s[0][0]
        dy = s[-1][1] - s[0][1]
        if dx < -1.0: s = s[::-1]
        elif abs(dx) < 1.0 and dy < -1.0: s = s[::-1]
        normalized.append(s)
    return normalized

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
        self.root.title("Murmure - Enhanced Graphical Path Generator")

        self.font = None
        self.target_text = ""
        self.all_strokes = []
        self.assignments = [] # List of {'stroke_id': int, 'verse': str}
        self.unassigned_verses = []
        self.selected_stroke_id = -1

        # View transformations
        self.view_scale = 1.0
        self.view_offset_x = 0
        self.view_offset_y = 0

        self.char_spacing = 80
        self.glyph_scale = 2.5
        self.verse_scale = 0.35

        # Pan state
        self.last_mouse_x = 0
        self.last_mouse_y = 0

        self.setup_ui()
        self.load_default_font()

    def setup_ui(self):
        # Top frame
        top_frame = tk.Frame(self.root)
        top_frame.pack(side=tk.TOP, fill=tk.X, padx=5, pady=5)

        tk.Label(top_frame, text="Target Text:").pack(side=tk.LEFT)
        self.text_entry = tk.Entry(top_frame, width=30)
        self.text_entry.pack(side=tk.LEFT, padx=5)
        self.text_entry.bind("<Return>", lambda e: self.update_text())

        tk.Button(top_frame, text="Update Text", command=self.update_text).pack(side=tk.LEFT, padx=2)
        tk.Button(top_frame, text="Load Font", command=self.browse_font).pack(side=tk.LEFT, padx=2)
        tk.Button(top_frame, text="Reset View", command=self.reset_view).pack(side=tk.LEFT, padx=2)

        # Main area
        main_frame = tk.Frame(self.root)
        main_frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        # Left: Canvas
        canvas_frame = tk.Frame(main_frame)
        canvas_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.canvas = tk.Canvas(canvas_frame, bg="black", width=800, height=500)
        self.canvas.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        # Canvas bindings
        self.canvas.bind("<Button-1>", self.on_canvas_click)
        self.canvas.bind("<Button-3>", self.start_pan)
        self.canvas.bind("<B3-Motion>", self.do_pan)
        self.canvas.bind("<MouseWheel>", self.on_mouse_wheel) # Windows/macOS
        self.canvas.bind("<Button-4>", self.on_mouse_wheel)   # Linux scroll up
        self.canvas.bind("<Button-5>", self.on_mouse_wheel)   # Linux scroll down

        # Right: Lists and Controls
        right_frame = tk.Frame(main_frame, width=300)
        right_frame.pack(side=tk.RIGHT, fill=tk.Y, padx=5)

        # Unassigned Verses List
        tk.Label(right_frame, text="Unassigned Verses:").pack(side=tk.TOP)
        self.unassigned_list = tk.Listbox(right_frame, width=40, height=8)
        self.unassigned_list.pack(side=tk.TOP, fill=tk.X)
        self.unassigned_list.bind("<<ListboxSelect>>", self.on_unassigned_select)

        # Assigned Verses List
        tk.Label(right_frame, text="Assigned Verses (ID | Verse):").pack(side=tk.TOP, pady=(10, 0))
        self.assigned_list = tk.Listbox(right_frame, width=40, height=12)
        self.assigned_list.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        self.assigned_list.bind("<<ListboxSelect>>", self.on_assigned_select)

        # Verse Text Entry
        tk.Label(right_frame, text="Verse Editor:").pack(side=tk.TOP, pady=(10, 0))
        self.verse_entry = tk.Entry(right_frame, width=40)
        self.verse_entry.pack(side=tk.TOP, fill=tk.X)

        btn_frame = tk.Frame(right_frame)
        btn_frame.pack(side=tk.TOP, fill=tk.X, pady=5)

        tk.Button(btn_frame, text="Add New Verse", command=self.add_unassigned).pack(side=tk.LEFT, expand=True, fill=tk.X)
        tk.Button(btn_frame, text="Assign Selected", command=self.assign_selected).pack(side=tk.LEFT, expand=True, fill=tk.X)

        tk.Button(right_frame, text="Unassign Selected", command=self.unassign_selected).pack(side=tk.TOP, fill=tk.X)
        tk.Button(right_frame, text="Delete Selected Verse", command=self.delete_verse).pack(side=tk.TOP, fill=tk.X)

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
                if self.target_text: self.update_text()
            except Exception as e:
                messagebox.showerror("Error", f"Could not load font: {e}")

    def reset_view(self):
        self.view_scale = 1.0
        self.view_offset_x = 50
        self.view_offset_y = 250
        self.draw_everything()

    def world_to_screen(self, x, y):
        return (x * self.view_scale + self.view_offset_x,
                y * self.view_scale + self.view_offset_y)

    def screen_to_world(self, sx, sy):
        return ((sx - self.view_offset_x) / self.view_scale,
                (sy - self.view_offset_y) / self.view_scale)

    def update_text(self):
        text = self.text_entry.get()
        if not text or not self.font: return
        self.target_text = text
        self.all_strokes = []

        x_cursor = 0
        stroke_id = 0
        for char in text:
            if char == ' ':
                x_cursor += self.char_spacing
                continue
            lines = list(self.font.lines_for_text(char))
            scaled = [((p1[0]*self.glyph_scale, p1[1]*self.glyph_scale), (p2[0]*self.glyph_scale, p2[1]*self.glyph_scale)) for p1, p2 in lines]
            if not scaled:
                x_cursor += 20
                continue
            min_x = min(p[0] for line in scaled for p in line)
            max_x = max(p[0] for line in scaled for p in line)
            for stroke in get_strokes(scaled):
                adj = [(p[0] - min_x + x_cursor, p[1]) for p in stroke]
                self.all_strokes.append({'char': char, 'stroke': adj, 'id': stroke_id})
                stroke_id += 1
            x_cursor += (max_x - min_x) + 25

        self.draw_everything()

    def draw_everything(self):
        self.canvas.delete("all")
        # Draw target text strokes
        for s in self.all_strokes:
            pts = s['stroke']
            color = "#000044" # Dim blue for strokes
            width = 1

            if s['id'] == self.selected_stroke_id:
                color = "yellow"
                width = 2

            screen_pts = [self.world_to_screen(p[0], p[1]) for p in pts]
            for i in range(len(screen_pts)-1):
                self.canvas.create_line(screen_pts[i][0], screen_pts[i][1], screen_pts[i+1][0], screen_pts[i+1][1], fill=color, width=width)

            # Stroke ID Label
            mid_p = screen_pts[len(screen_pts)//2]
            self.canvas.create_text(mid_p[0], mid_p[1]-10, text=str(s['id']), fill="#555555", font=("Arial", 7))

        # Draw assigned verses following strokes (Curved Preview)
        stroke_usage = {}
        for idx, a in enumerate(self.assignments):
            s_id = a['stroke_id']
            try:
                s_info = next(s for s in self.all_strokes if s['id'] == s_id)
            except StopIteration: continue

            reused_idx = stroke_usage.get(s_id, 0)
            total_reused = sum(1 for aa in self.assignments if aa['stroke_id'] == s_id)
            stroke_usage[s_id] = reused_idx + 1

            y_adj = (reused_idx - 0.5 * (total_reused-1)) * 15
            self.draw_preview_verse(a['verse'], s_info['stroke'], y_adj)

    def draw_preview_verse(self, text, stroke, y_adj):
        samples = sample_stroke(stroke, len(text))
        color = "#00FF00" if text else "#888888"

        for i, char in enumerate(text):
            if i >= len(samples): break
            wx, wy, wa = samples[i]
            wy += y_adj

            # Get character strokes from font
            char_lines = list(self.font.lines_for_text(char))

            # Transform each line of the character
            for p1, p2 in char_lines:
                # 1. Scale character
                p1s = (p1[0] * self.verse_scale, p1[1] * self.verse_scale)
                p2s = (p2[0] * self.verse_scale, p2[1] * self.verse_scale)

                # 2. Rotate character
                def rot(p, angle):
                    c, s = math.cos(angle), math.sin(angle)
                    return (p[0]*c - p[1]*s, p[0]*s + p[1]*c)

                p1r = rot(p1s, wa)
                p2r = rot(p2s, wa)

                # 3. Translate to world position
                p1w = (p1r[0] + wx, p1r[1] + wy)
                p2w = (p2r[0] + wx, p2r[1] + wy)

                # 4. Map to screen
                s1 = self.world_to_screen(p1w[0], p1w[1])
                s2 = self.world_to_screen(p2w[0], p2w[1])

                self.canvas.create_line(s1[0], s1[1], s2[0], s2[1], fill=color, width=1)

    def on_canvas_click(self, event):
        wx, wy = self.screen_to_world(event.x, event.y)
        closest_id = -1
        min_dist = 20 / self.view_scale

        for s in self.all_strokes:
            for i in range(len(s['stroke'])-1):
                dist = self.dist_to_segment((wx, wy), s['stroke'][i], s['stroke'][i+1])
                if dist < min_dist:
                    min_dist = dist
                    closest_id = s['id']

        self.selected_stroke_id = closest_id
        self.draw_everything()

        if closest_id != -1:
            self.status_label.config(text=f"Selected Stroke ID: {closest_id}")
            # Highlight assigned verse if exists
            for i, a in enumerate(self.assignments):
                if a['stroke_id'] == closest_id:
                    self.assigned_list.selection_clear(0, tk.END)
                    self.assigned_list.selection_set(i)
                    self.verse_entry.delete(0, tk.END)
                    self.verse_entry.insert(0, a['verse'])
                    return
        self.assigned_list.selection_clear(0, tk.END)

    def dist_to_segment(self, p, s1, s2):
        px, py = p; x1, y1 = s1; x2, y2 = s2
        dx, dy = x2-x1, y2-y1
        if dx == 0 and dy == 0: return math.sqrt((px-x1)**2 + (py-y1)**2)
        t = max(0, min(1, ((px-x1)*dx + (py-y1)*dy) / (dx*dx + dy*dy)))
        return math.sqrt((px-(x1 + t*dx))**2 + (py-(y1 + t*dy))**2)

    # Panning and Zooming
    def start_pan(self, event):
        self.last_mouse_x = event.x
        self.last_mouse_y = event.y

    def do_pan(self, event):
        dx = event.x - self.last_mouse_x
        dy = event.y - self.last_mouse_y
        self.view_offset_x += dx
        self.view_offset_y += dy
        self.last_mouse_x = event.x
        self.last_mouse_y = event.y
        self.draw_everything()

    def on_mouse_wheel(self, event):
        if event.num == 4 or event.delta > 0: factor = 1.1
        else: factor = 0.9

        # Zoom relative to mouse position
        mx, my = event.x, event.y
        wx, wy = self.screen_to_world(mx, my)
        self.view_scale *= factor
        # Re-offset so that (wx, wy) stays under (mx, my)
        nsx, nsy = self.world_to_screen(wx, wy)
        self.view_offset_x += (mx - nsx)
        self.view_offset_y += (my - nsy)
        self.draw_everything()

    # Verse Management
    def update_verse_lists(self):
        self.unassigned_list.delete(0, tk.END)
        for v in self.unassigned_verses:
            self.unassigned_list.insert(tk.END, v)
        self.assigned_list.delete(0, tk.END)
        for a in self.assignments:
            self.assigned_list.insert(tk.END, f"{a['stroke_id']} | {a['verse']}")

    def on_unassigned_select(self, event):
        sel = self.unassigned_list.curselection()
        if sel:
            self.verse_entry.delete(0, tk.END)
            self.verse_entry.insert(0, self.unassigned_verses[sel[0]])

    def on_assigned_select(self, event):
        sel = self.assigned_list.curselection()
        if sel:
            a = self.assignments[sel[0]]
            self.selected_stroke_id = a['stroke_id']
            self.verse_entry.delete(0, tk.END)
            self.verse_entry.insert(0, a['verse'])
            self.draw_everything()

    def add_unassigned(self):
        txt = self.verse_entry.get()
        if txt:
            self.unassigned_verses.append(txt)
            self.update_verse_lists()
            self.verse_entry.delete(0, tk.END)

    def assign_selected(self):
        if self.selected_stroke_id == -1:
            messagebox.showwarning("Warning", "Click a stroke on the canvas first.")
            return

        # Try taking from unassigned first
        sel_un = self.unassigned_list.curselection()
        if sel_un:
            txt = self.unassigned_verses.pop(sel_un[0])
        else:
            txt = self.verse_entry.get()

        if not txt: return

        # Move existing assignment back to unassigned if overwriting
        for a in self.assignments:
            if a['stroke_id'] == self.selected_stroke_id:
                self.unassigned_verses.append(a['verse'])
                self.assignments.remove(a)
                break

        self.assignments.append({'stroke_id': self.selected_stroke_id, 'verse': txt})
        self.update_verse_lists()
        self.draw_everything()

    def unassign_selected(self):
        sel = self.assigned_list.curselection()
        if not sel: return
        a = self.assignments.pop(sel[0])
        self.unassigned_verses.append(a['verse'])
        self.update_verse_lists()
        self.draw_everything()

    def delete_verse(self):
        sel_un = self.unassigned_list.curselection()
        if sel_un:
            self.unassigned_verses.pop(sel_un[0])
        else:
            sel_as = self.assigned_list.curselection()
            if sel_as: self.assignments.pop(sel_as[0])
        self.update_verse_lists()
        self.draw_everything()

    # Persistence
    def save_project(self):
        if not self.target_text: return
        filename = filedialog.asksaveasfilename(title="Save glyph_paths.h", defaultextension=".h", filetypes=[("Header files", "*.h")])
        if not filename: return

        try:
            all_pts = []; v_off = []; v_len = []; v_texts = []
            stroke_usage = {}
            for idx, a in enumerate(self.assignments):
                s_id = a['stroke_id']
                try:
                    s_info = next(s for s in self.all_strokes if s['id'] == s_id)
                except StopIteration: continue

                txt = a['verse']
                n = len(txt)
                samples = sample_stroke(s_info['stroke'], n)

                reused_idx = stroke_usage.get(s_id, 0)
                total_reused = sum(1 for aa in self.assignments if aa['stroke_id'] == s_id)
                stroke_usage[s_id] = reused_idx + 1
                y_adj = (reused_idx - 0.5 * (total_reused-1)) * 15

                v_off.append(len(all_pts)); v_len.append(n); v_texts.append(txt)
                for i in range(n):
                    x, y, a_rad = samples[i]
                    all_pts.append((x, y + y_adj, a_rad, self.verse_scale))

            with open(filename, "w") as f:
                f.write("// AUTO-GENERATED - ENHANCED INTERACTIVE GUI\n")
                f.write(f"// META: TARGET_TEXT: {self.target_text}\n")
                for a in self.assignments:
                    f.write(f"// META: ASSIGN: {a['stroke_id']} | {a['verse']}\n")
                for v in self.unassigned_verses:
                    f.write(f"// META: UNASSIGNED: {v}\n")
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
            messagebox.showinfo("Success", f"Project saved to {filename}")
        except Exception as e:
            messagebox.showerror("Error", f"Could not save project: {e}")

    def load_project(self):
        filename = filedialog.askopenfilename(title="Open glyph_paths.h", filetypes=[("Header files", "*.h"), ("All files", "*.*")])
        if not filename: return
        try:
            with open(filename, "r") as f: content = f.read()
            target_match = re.search(r"// META: TARGET_TEXT: (.*)", content)
            if target_match:
                self.text_entry.delete(0, tk.END)
                self.text_entry.insert(0, target_match.group(1).strip())
                self.update_text()
            else:
                messagebox.showerror("Error", "No target text metadata found.")
                return

            self.assignments = []
            for s_id, verse in re.findall(r"// META: ASSIGN: (\d+) \| (.*)", content):
                self.assignments.append({'stroke_id': int(s_id), 'verse': verse})

            self.unassigned_verses = re.findall(r"// META: UNASSIGNED: (.*)", content)

            self.update_verse_lists()
            self.reset_view()
            self.status_label.config(text=f"Loaded project: {os.path.basename(filename)}")
        except Exception as e:
            messagebox.showerror("Error", f"Could not load project: {e}")

if __name__ == "__main__":
    root = tk.Tk()
    app = GUIGenerator(root)
    root.mainloop()
