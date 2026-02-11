
import tkinter as tk
from tkinter import messagebox, filedialog
import math
import re
import os
from hershey_parser import load_font

# Helper functions for stroke management
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
        if dx < -1.0: s = s[::-1]
        elif abs(dx) < 1.0 and dy < -1.0: s = s[::-1]
        normalized.append(s)
    return normalized

def get_stroke_length(stroke):
    return sum(math.sqrt((stroke[i+1][0]-stroke[i][0])**2 + (stroke[i+1][1]-stroke[i][1])**2) for i in range(len(stroke)-1))

def sample_stroke(stroke, num_samples):
    total_l = get_stroke_length(stroke)
    if num_samples <= 0: return []

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

    samples = []
    for i in range(len(pts)):
        if len(pts) > 1:
            if i == 0: p1, p2 = pts[0], pts[1]
            elif i == len(pts) - 1: p1, p2 = pts[-2], pts[-1]
            else: p1, p2 = pts[i-1], pts[i+1]
            angle = math.atan2(p2[1]-p1[1], p2[0]-p1[0])
        else:
            angle = math.atan2(stroke[-1][1]-stroke[0][1], stroke[-1][0]-stroke[0][0]) if len(stroke) > 1 else 0
        samples.append((pts[i][0], pts[i][1], angle))
    return samples

class GUIGenerator:
    def __init__(self, root):
        self.root = root
        self.root.title("Murmure - Enhanced Graphical Path Generator")

        self.glyph_width = 16
        self.glyph_height = 24
        self.glyph_chars = ""
        self.glyph_bitmaps = []

        self.view_scale = 1.0
        self.view_offset_x = 50
        self.view_offset_y = 250

        self.load_arduino_tables()

        self.font = None
        self.target_text = ""
        self.all_strokes = []
        self.assignments = [] # List of {'stroke_id': int, 'verse': str}
        self.unassigned_verses = []
        self.selected_stroke_id = -1
        self.selected_point_idx = -1
        self.edit_mode = False # False: Verse Assignment, True: Stroke Edit

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

        self.edit_mode_var = tk.BooleanVar(value=False)
        tk.Checkbutton(top_frame, text="Stroke Edit", variable=self.edit_mode_var, command=self.toggle_edit_mode).pack(side=tk.LEFT, padx=10)

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
        self.canvas.bind("<B1-Motion>", self.on_canvas_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_canvas_release)
        self.canvas.bind("<Button-3>", self.start_pan)
        self.canvas.bind("<B3-Motion>", self.do_pan)
        self.canvas.bind("<MouseWheel>", self.on_mouse_wheel) # Windows/macOS
        self.canvas.bind("<Button-4>", self.on_mouse_wheel)   # Linux scroll up
        self.canvas.bind("<Button-5>", self.on_mouse_wheel)   # Linux scroll down

        # Right: Lists and Controls
        self.right_frame = tk.Frame(main_frame, width=300)
        self.right_frame.pack(side=tk.RIGHT, fill=tk.Y, padx=5)

        # Verse Mode UI (parent: right_frame)
        self.verse_mode_frame = tk.Frame(self.right_frame)
        self.verse_mode_frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        tk.Label(self.verse_mode_frame, text="Unassigned Verses:").pack(side=tk.TOP)
        self.unassigned_list = tk.Listbox(self.verse_mode_frame, width=40, height=8)
        self.unassigned_list.pack(side=tk.TOP, fill=tk.X)
        self.unassigned_list.bind("<<ListboxSelect>>", self.on_unassigned_select)

        tk.Label(self.verse_mode_frame, text="Assigned Verses (ID | Verse):").pack(side=tk.TOP, pady=(10, 0))
        self.assigned_list = tk.Listbox(self.verse_mode_frame, width=40, height=12)
        self.assigned_list.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        self.assigned_list.bind("<<ListboxSelect>>", self.on_assigned_select)

        tk.Label(self.verse_mode_frame, text="Verse Editor:").pack(side=tk.TOP, pady=(10, 0))
        self.verse_entry = tk.Entry(self.verse_mode_frame, width=40)
        self.verse_entry.pack(side=tk.TOP, fill=tk.X)

        btn_frame = tk.Frame(self.verse_mode_frame)
        btn_frame.pack(side=tk.TOP, fill=tk.X, pady=5)

        tk.Button(btn_frame, text="Add New Verse", command=self.add_unassigned).pack(side=tk.LEFT, expand=True, fill=tk.X)
        tk.Button(btn_frame, text="Assign Selected", command=self.assign_selected).pack(side=tk.LEFT, expand=True, fill=tk.X)

        tk.Button(self.verse_mode_frame, text="Unassign Selected", command=self.unassign_selected).pack(side=tk.TOP, fill=tk.X)
        tk.Button(self.verse_mode_frame, text="Delete Selected Verse", command=self.delete_verse).pack(side=tk.TOP, fill=tk.X)

        # Stroke Edit Mode UI (parent: right_frame, initially hidden)
        self.stroke_mode_frame = tk.Frame(self.right_frame)
        # Initially hidden

        tk.Label(self.stroke_mode_frame, text="Strokes:").pack(side=tk.TOP)
        self.stroke_list = tk.Listbox(self.stroke_mode_frame, width=40, height=10)
        self.stroke_list.pack(side=tk.TOP, fill=tk.X)
        self.stroke_list.bind("<<ListboxSelect>>", self.on_stroke_list_select)

        tk.Label(self.stroke_mode_frame, text="Selected Stroke Points:").pack(side=tk.TOP, pady=(10,0))
        self.point_list = tk.Listbox(self.stroke_mode_frame, width=40, height=10)
        self.point_list.pack(side=tk.TOP, fill=tk.X)
        self.point_list.bind("<<ListboxSelect>>", self.on_point_list_select)

        edit_p_frame = tk.Frame(self.stroke_mode_frame)
        edit_p_frame.pack(side=tk.TOP, fill=tk.X, pady=5)
        tk.Label(edit_p_frame, text="X:").pack(side=tk.LEFT)
        self.point_x_entry = tk.Entry(edit_p_frame, width=8)
        self.point_x_entry.pack(side=tk.LEFT, padx=2)
        tk.Label(edit_p_frame, text="Y:").pack(side=tk.LEFT)
        self.point_y_entry = tk.Entry(edit_p_frame, width=8)
        self.point_y_entry.pack(side=tk.LEFT, padx=2)
        tk.Button(edit_p_frame, text="Set", command=self.set_point_coords).pack(side=tk.LEFT, padx=2)

        stroke_btn_frame = tk.Frame(self.stroke_mode_frame)
        stroke_btn_frame.pack(side=tk.TOP, fill=tk.X, pady=5)
        tk.Button(stroke_btn_frame, text="Move Up", command=lambda: self.move_stroke_delta(0, -5)).pack(side=tk.LEFT, expand=True, fill=tk.X)
        tk.Button(stroke_btn_frame, text="Move Down", command=lambda: self.move_stroke_delta(0, 5)).pack(side=tk.LEFT, expand=True, fill=tk.X)
        tk.Button(stroke_btn_frame, text="Move Left", command=lambda: self.move_stroke_delta(-5, 0)).pack(side=tk.LEFT, expand=True, fill=tk.X)
        tk.Button(stroke_btn_frame, text="Move Right", command=lambda: self.move_stroke_delta(5, 0)).pack(side=tk.LEFT, expand=True, fill=tk.X)

        tk.Button(self.stroke_mode_frame, text="Delete Point", command=self.delete_point).pack(side=tk.TOP, fill=tk.X)
        tk.Button(self.stroke_mode_frame, text="Add Point After", command=self.add_point).pack(side=tk.TOP, fill=tk.X)
        tk.Button(self.stroke_mode_frame, text="Reverse Stroke", command=self.reverse_selected_stroke).pack(side=tk.TOP, fill=tk.X)
        tk.Button(self.stroke_mode_frame, text="Delete Stroke", command=self.delete_stroke).pack(side=tk.TOP, fill=tk.X)

        # Bottom frame
        bottom_frame = tk.Frame(self.root)
        bottom_frame.pack(side=tk.BOTTOM, fill=tk.X, padx=5, pady=5)

        tk.Button(bottom_frame, text="Load Project (glyph_paths.h)", command=self.load_project).pack(side=tk.LEFT, padx=5)
        tk.Button(bottom_frame, text="Save Project (glyph_paths.h)", command=self.save_project).pack(side=tk.LEFT, padx=5)

        self.status_label = tk.Label(bottom_frame, text="Ready", bd=1, relief=tk.SUNKEN, anchor=tk.W)
        self.status_label.pack(side=tk.RIGHT, fill=tk.X, expand=True)

    def load_arduino_tables(self):
        try:
            with open("arduino_tables.h", "r") as f:
                content = f.read()

            w_match = re.search(r"#define GLYPH_WIDTH (\d+)", content)
            h_match = re.search(r"#define GLYPH_HEIGHT (\d+)", content)
            if w_match: self.glyph_width = int(w_match.group(1))
            if h_match: self.glyph_height = int(h_match.group(1))

            # Use a more robust regex for C string literals that might contain escaped quotes
            list_match = re.search(r'GLYPH_CHAR_LIST\[\d+\] PROGMEM = ("(?:[^"\\]|\\.)*");', content)
            if list_match:
                raw_list = list_match.group(1)
                # Use Python's literal_eval to safely parse the C-style string literal
                # (which is mostly compatible with Python's string literal)
                import ast
                try:
                    self.glyph_chars = ast.literal_eval(raw_list)
                except:
                    # Fallback to manual replacement if literal_eval fails
                    if raw_list.startswith('"') and raw_list.endswith('"'):
                        raw_list = raw_list[1:-1]
                    self.glyph_chars = raw_list.replace('\\"', '"').replace("\\'", "'").replace("\\\\", "\\")

            # Extract bitmaps
            bitmap_block = re.search(r"GLYPH_BITMAPS\[\d+\] PROGMEM = \{(.*?)\};", content, re.DOTALL)
            if bitmap_block:
                vals = re.findall(r"0x[0-9A-Fa-f]+|\d+", bitmap_block.group(1))
                self.glyph_bitmaps = [int(v, 0) for v in vals]

            if not self.glyph_chars or not self.glyph_bitmaps:
                print("Warning: arduino_tables.h loaded but glyphs seem empty.")

        except Exception as e:
            print(f"Error loading arduino_tables.h: {e}")

    def load_default_font(self):
        for name in ["futural", "futuram", "timesr"]:
            try:
                self.font = load_font(name)
                self.status_label.config(text=f"Font loaded: {name}")
                return
            except:
                continue
        self.status_label.config(text="Error: Could not load any default font. Please use 'Load Font'.")

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

    def toggle_edit_mode(self):
        self.edit_mode = self.edit_mode_var.get()
        if self.edit_mode:
            self.verse_mode_frame.pack_forget()
            self.stroke_mode_frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
            self.update_stroke_list()
        else:
            self.stroke_mode_frame.pack_forget()
            self.verse_mode_frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
            self.update_verse_lists()
        self.draw_everything()

    def update_text(self):
        text_raw = self.text_entry.get()
        if not text_raw: return
        if not self.font:
            self.status_label.config(text="Error: No font loaded.")
            return

        if not hasattr(self.font, 'strokes_for_text'):
            self.status_label.config(text="Error: Font object missing 'strokes_for_text'.")
            print(f"DEBUG: font type: {type(self.font)}, dir: {dir(self.font)}")
            return

        self.target_text = text_raw
        self.all_strokes = []

        # Support \n for multi-line
        lines_text = text_raw.split("\\n")

        stroke_id = 0
        y_cursor = 0
        line_height = 60

        for line in lines_text:
            x_cursor = 0
            for char in line:
                if char == ' ':
                    x_cursor += self.char_spacing
                    continue
                raw_strokes = list(self.font.strokes_for_text(char))
                scaled_strokes = [[(p[0]*self.glyph_scale, p[1]*self.glyph_scale) for p in s] for s in raw_strokes]
                if not scaled_strokes:
                    x_cursor += 20
                    continue
                min_x = min(p[0] for s in scaled_strokes for p in s)
                max_x = max(p[0] for s in scaled_strokes for p in s)

                # Adjust strokes to start from 0 for this character
                adj_strokes = [[(p[0] - min_x, p[1]) for p in s] for s in scaled_strokes]

                for stroke in get_strokes(adj_strokes):
                    final_stroke = [(p[0] + x_cursor, p[1] + y_cursor) for p in stroke]
                    self.all_strokes.append({'char': char, 'stroke': final_stroke, 'id': stroke_id})
                    stroke_id += 1
                x_cursor += (max_x - min_x) + 25
            y_cursor += line_height

        if self.edit_mode:
            self.update_stroke_list()
        self.draw_everything()

    def draw_everything(self):
        self.canvas.delete("all")
        # Draw target text strokes
        for s in self.all_strokes:
            pts = s['stroke']
            color = "#4444AA" # Brighter blue for strokes
            width = 1

            is_selected = (s['id'] == self.selected_stroke_id)
            if is_selected:
                color = "yellow"
                width = 2

            screen_pts = [self.world_to_screen(p[0], p[1]) for p in pts]
            for i in range(len(screen_pts)-1):
                self.canvas.create_line(screen_pts[i][0], screen_pts[i][1], screen_pts[i+1][0], screen_pts[i+1][1], fill=color, width=width)

            # Draw points if in edit mode and stroke is selected
            if self.edit_mode and is_selected:
                for i, p in enumerate(screen_pts):
                    p_color = "red" if i == self.selected_point_idx else "orange"
                    r = 3 if i == self.selected_point_idx else 2
                    self.canvas.create_oval(p[0]-r, p[1]-r, p[0]+r, p[1]+r, fill=p_color, outline=p_color)

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

        gw, gh = self.glyph_width, self.glyph_height

        for i, char in enumerate(text):
            if i >= len(samples): break
            wx, wy, wa = samples[i]
            wy += y_adj

            idx = self.glyph_chars.find(char)
            if idx < 0: continue

            bitmap = self.glyph_bitmaps[idx * gw : (idx + 1) * gw]

            cos_a = math.cos(wa)
            sin_a = math.sin(wa)

            # Draw bitmap pixels transformed
            for col in range(gw):
                col_val = bitmap[col]
                if not col_val: continue
                for row in range(gh):
                    if col_val & (1 << row):
                        # Local coords relative to glyph center
                        sx = (col - gw / 2) * self.verse_scale
                        sy = (row - gh / 2) * self.verse_scale

                        # Rotate
                        rx = sx * cos_a - sy * sin_a
                        ry = sx * sin_a + sy * cos_a

                        # Translate & Project to screen
                        px, py = self.world_to_screen(wx + rx, wy + ry)

                        # Optimization: if too small, skip or draw single pixel
                        if self.view_scale * self.verse_scale > 2:
                            r = max(1, int(self.view_scale * self.verse_scale / 2))
                            self.canvas.create_rectangle(px-r, py-r, px+r, py+r, fill=color, outline="")
                        else:
                            self.canvas.create_line(px, py, px+1, py, fill=color)

    def on_canvas_click(self, event):
        wx, wy = self.screen_to_world(event.x, event.y)
        self.drag_start_world = (wx, wy)
        self.is_dragging = False

        # In edit mode, prioritize point selection if a stroke is already selected
        if self.edit_mode and self.selected_stroke_id != -1:
            try:
                s = next(s for s in self.all_strokes if s['id'] == self.selected_stroke_id)
                for i, p in enumerate(s['stroke']):
                    d = math.sqrt((p[0]-wx)**2 + (p[1]-wy)**2)
                    if d < 10 / self.view_scale:
                        self.selected_point_idx = i
                        self.is_dragging = True
                        self.update_point_selection()
                        self.draw_everything()
                        return
            except StopIteration: pass

        closest_id = -1
        min_dist = 20 / self.view_scale

        for s in self.all_strokes:
            for i in range(len(s['stroke'])-1):
                dist = self.dist_to_segment((wx, wy), s['stroke'][i], s['stroke'][i+1])
                if dist < min_dist:
                    min_dist = dist
                    closest_id = s['id']

        self.selected_stroke_id = closest_id
        self.selected_point_idx = -1

        if self.edit_mode:
            self.update_stroke_selection()

        self.draw_everything()

        if closest_id != -1:
            self.status_label.config(text=f"Selected Stroke ID: {closest_id}")
            if not self.edit_mode:
                # Highlight assigned verse if exists
                for i, a in enumerate(self.assignments):
                    if a['stroke_id'] == closest_id:
                        self.assigned_list.selection_clear(0, tk.END)
                        self.assigned_list.selection_set(i)
                        self.verse_entry.delete(0, tk.END)
                        self.verse_entry.insert(0, a['verse'])
                        return
        if not self.edit_mode:
            self.assigned_list.selection_clear(0, tk.END)

    def on_canvas_drag(self, event):
        if not self.edit_mode or self.selected_stroke_id == -1: return

        wx, wy = self.screen_to_world(event.x, event.y)
        dx = wx - self.drag_start_world[0]
        dy = wy - self.drag_start_world[1]

        try:
            s = next(s for s in self.all_strokes if s['id'] == self.selected_stroke_id)
            if self.selected_point_idx != -1:
                # Drag individual point
                orig_p = s['stroke'][self.selected_point_idx]
                s['stroke'][self.selected_point_idx] = (orig_p[0] + dx, orig_p[1] + dy)
            else:
                # Drag whole stroke
                s['stroke'] = [(p[0]+dx, p[1]+dy) for p in s['stroke']]

            self.drag_start_world = (wx, wy)
            self.is_dragging = True
            self.draw_everything()
            self.update_point_list()
        except StopIteration: pass

    def on_canvas_release(self, event):
        if self.is_dragging:
            self.update_point_selection()
            self.is_dragging = False

    def dist_to_segment(self, p, s1, s2):
        px, py = p; x1, y1 = s1; x2, y2 = s2
        dx, dy = x2-x1, y2-y1
        if dx == 0 and dy == 0: return math.sqrt((px-x1)**2 + (py-y1)**2)
        t = max(0, min(1, ((px-x1)*dx + (py-y1)*dy) / (dx*dx + dy*dy)))
        return math.sqrt((px-(x1 + t*dx))**2 + (py-(y1 + t*dy))**2)

    # Stroke/Point Editing Methods
    def update_stroke_list(self):
        self.stroke_list.delete(0, tk.END)
        for s in self.all_strokes:
            self.stroke_list.insert(tk.END, f"ID: {s['id']} | Char: {s['char']} | Pts: {len(s['stroke'])}")
        self.update_stroke_selection()

    def update_stroke_selection(self):
        self.stroke_list.selection_clear(0, tk.END)
        for i, s in enumerate(self.all_strokes):
            if s['id'] == self.selected_stroke_id:
                self.stroke_list.selection_set(i)
                self.stroke_list.see(i)
                self.update_point_list()
                return
        self.point_list.delete(0, tk.END)

    def on_stroke_list_select(self, event):
        sel = self.stroke_list.curselection()
        if sel:
            self.selected_stroke_id = self.all_strokes[sel[0]]['id']
            self.selected_point_idx = -1
            self.update_point_list()
            self.draw_everything()

    def update_point_list(self):
        self.point_list.delete(0, tk.END)
        try:
            s = next(s for s in self.all_strokes if s['id'] == self.selected_stroke_id)
            for i, p in enumerate(s['stroke']):
                self.point_list.insert(tk.END, f"{i}: ({p[0]:.1f}, {p[1]:.1f})")
        except StopIteration: pass

    def update_point_selection(self):
        self.point_list.selection_clear(0, tk.END)
        if self.selected_point_idx != -1:
            self.point_list.selection_set(self.selected_point_idx)
            self.point_list.see(self.selected_point_idx)
            # Update entries
            try:
                s = next(s for s in self.all_strokes if s['id'] == self.selected_stroke_id)
                p = s['stroke'][self.selected_point_idx]
                self.point_x_entry.delete(0, tk.END)
                self.point_x_entry.insert(0, f"{p[0]:.1f}")
                self.point_y_entry.delete(0, tk.END)
                self.point_y_entry.insert(0, f"{p[1]:.1f}")
            except StopIteration: pass

    def on_point_list_select(self, event):
        sel = self.point_list.curselection()
        if sel:
            self.selected_point_idx = sel[0]
            self.update_point_selection()
            self.draw_everything()

    def set_point_coords(self):
        if self.selected_stroke_id == -1 or self.selected_point_idx == -1: return
        try:
            x = float(self.point_x_entry.get())
            y = float(self.point_y_entry.get())
            s = next(s for s in self.all_strokes if s['id'] == self.selected_stroke_id)
            s['stroke'][self.selected_point_idx] = (x, y)
            self.update_point_list()
            self.update_point_selection()
            self.draw_everything()
        except ValueError: pass

    def move_stroke_delta(self, dx, dy):
        if self.selected_stroke_id == -1: return
        try:
            s = next(s for s in self.all_strokes if s['id'] == self.selected_stroke_id)
            s['stroke'] = [(p[0]+dx, p[1]+dy) for p in s['stroke']]
            self.update_point_list()
            self.update_point_selection()
            self.draw_everything()
        except StopIteration: pass

    def delete_point(self):
        if self.selected_stroke_id == -1 or self.selected_point_idx == -1: return
        s = next(s for s in self.all_strokes if s['id'] == self.selected_stroke_id)
        if len(s['stroke']) <= 2:
            messagebox.showwarning("Warning", "Stroke must have at least 2 points.")
            return
        s['stroke'].pop(self.selected_point_idx)
        self.selected_point_idx = -1
        self.update_point_list()
        self.draw_everything()

    def add_point(self):
        if self.selected_stroke_id == -1: return
        s = next(s for s in self.all_strokes if s['id'] == self.selected_stroke_id)
        if self.selected_point_idx == -1:
            # Add to end
            p_last = s['stroke'][-1]
            s['stroke'].append((p_last[0]+10, p_last[1]+10))
        else:
            p_curr = s['stroke'][self.selected_point_idx]
            if self.selected_point_idx < len(s['stroke']) - 1:
                p_next = s['stroke'][self.selected_point_idx + 1]
                p_new = ((p_curr[0]+p_next[0])/2, (p_curr[1]+p_next[1])/2)
                s['stroke'].insert(self.selected_point_idx + 1, p_new)
            else:
                s['stroke'].append((p_curr[0]+10, p_curr[1]+10))
        self.update_point_list()
        self.draw_everything()

    def reverse_selected_stroke(self):
        if self.selected_stroke_id == -1: return
        s = next(s for s in self.all_strokes if s['id'] == self.selected_stroke_id)
        s['stroke'] = s['stroke'][::-1]
        self.update_point_list()
        self.draw_everything()

    def delete_stroke(self):
        if self.selected_stroke_id == -1: return
        if not messagebox.askyesno("Confirm", "Delete this stroke?"): return
        self.all_strokes = [s for s in self.all_strokes if s['id'] != self.selected_stroke_id]
        self.selected_stroke_id = -1
        self.update_stroke_list()
        self.draw_everything()

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
                for s in self.all_strokes:
                    pts_str = " ".join([f"{p[0]},{p[1]}" for p in s['stroke']])
                    f.write(f"// META: STROKE: {s['id']} | {s['char']} | {pts_str}\n")
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
                self.target_text = target_match.group(1).strip()
            else:
                messagebox.showerror("Error", "No target text metadata found.")
                return

            # Prefer loading explicit strokes if present
            found_strokes = re.findall(r"// META: STROKE: (\d+) \| (.*?) \| (.*)", content)
            if found_strokes:
                self.all_strokes = []
                for s_id, char, pts_str in found_strokes:
                    pts = []
                    for pair in pts_str.split():
                        x, y = pair.split(",")
                        pts.append((float(x), float(y)))
                    self.all_strokes.append({'id': int(s_id), 'char': char, 'stroke': pts})
            else:
                self.update_text()

            self.assignments = []
            for s_id, verse in re.findall(r"// META: ASSIGN: (\d+) \| (.*)", content):
                self.assignments.append({'stroke_id': int(s_id), 'verse': verse})

            self.unassigned_verses = re.findall(r"// META: UNASSIGNED: (.*)", content)

            if self.edit_mode: self.update_stroke_list()
            else: self.update_verse_lists()

            self.reset_view()
            self.status_label.config(text=f"Loaded project: {os.path.basename(filename)}")
        except Exception as e:
            messagebox.showerror("Error", f"Could not load project: {e}")

if __name__ == "__main__":
    root = tk.Tk()
    app = GUIGenerator(root)
    root.mainloop()
