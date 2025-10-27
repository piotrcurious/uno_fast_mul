#!/usr/bin/env python3
"""
generate_tables_and_font.py

Generates:
 - numeric lookup tables (msb, log2, exp2, sin/cos, perspective, sphere, atan, stereo)
 - glyph bitmaps (monochrome) for the provided TTF/OTF font

Writes a header (and optional .c) with PROGMEM arrays that the ESP32 demo uses.

Requires: Pillow (pip install pillow)
"""
from pathlib import Path
import math, argparse, textwrap
from PIL import Image, ImageFont, ImageDraw

# ---------- Utilities ----------
def qscale(q): return 1 << q
def clamp_int(x, lo, hi): return max(lo, min(hi, int(x)))

def fmt_c_array(ctype, name, values, per_line=12, progmem_macro="PROGMEM"):
    lines = []
    for i in range(0, len(values), per_line):
        chunk = values[i:i+per_line]
        lines.append("  " + ", ".join(str(int(v)) for v in chunk) + ("," if i+per_line < len(values) else ""))
    body = "\n".join(lines)
    return f"const {ctype} {progmem_macro} {name}[{len(values)}] = {{\n{body}\n}};\n"

def fmt_c_array_extern(ctype, name, length, progmem_macro="PROGMEM"):
    return f"extern const {ctype} {progmem_macro} {name}[{length}];\n"

# ---------- Table generators ----------
def gen_msb_table(size=256):
    tbl = []
    for i in range(size):
        tbl.append(0 if i == 0 else int(math.floor(math.log2(i))))
    return tbl

def gen_log2_table(size=256, q=8):
    scale = qscale(q)
    tbl = []
    for i in range(size):
        if i < 1:
            tbl.append(0)
        else:
            v = round(math.log2(i) * scale)
            v = clamp_int(v, 0, 0xFFFF)
            tbl.append(v)
    return tbl

def gen_exp2_frac_table(frac_size=256, q=8):
    scale = qscale(q)
    tbl = []
    for f in range(frac_size):
        v = round((2 ** (f / frac_size)) * scale)
        v = clamp_int(v, 0, 0xFFFF)
        tbl.append(v)
    return tbl

def gen_sin_cos_table(n=512, q=15):
    scale = qscale(q)
    sin_tbl = []
    cos_tbl = []
    for i in range(n):
        angle = 2.0 * math.pi * i / n
        s = round(math.sin(angle) * scale)
        c = round(math.cos(angle) * scale)
        sin_tbl.append(clamp_int(s, -32768, 32767))
        cos_tbl.append(clamp_int(c, -32768, 32767))
    return sin_tbl, cos_tbl

def gen_perspective_table(n=256, q=8, focal=256.0, zmin=0.0, zmax=1024.0):
    scale = qscale(q)
    tbl = []
    for i in range(n):
        z = zmin + (zmax - zmin) * (i / (n - 1))
        denom = focal + z
        s = (focal / denom) if denom != 0.0 else 0.0
        v = round(s * scale)
        tbl.append(clamp_int(v, 0, 0xFFFFFFFF))
    return tbl

def gen_sphere_theta_tables(theta_steps=128, q=15):
    scale = qscale(q)
    sin_t = []
    cos_t = []
    for t in range(theta_steps):
        theta = math.pi * t / (theta_steps - 1)
        s = round(math.sin(theta) * scale)
        c = round(math.cos(theta) * scale)
        sin_t.append(clamp_int(s, -32768, 32767))
        cos_t.append(clamp_int(c, -32768, 32767))
    return sin_t, cos_t

def gen_atan_table(n=1024, q=15, x_range=4.0):
    scale = qscale(q)
    tbl = []
    for i in range(n):
        slope = ((i / (n - 1)) * 2.0 * x_range) - x_range
        v = round(math.atan(slope) * scale)
        tbl.append(clamp_int(v, -32768, 32767))
    return tbl

def gen_stereographic_table(n=256, q=12):
    scale = qscale(q)
    tbl = []
    rmax = 2.0
    for i in range(n):
        r = (i / (n - 1)) * rmax
        factor = 2.0 / (1.0 + r * r)
        tbl.append(round(factor * scale))
    return tbl

# ---------- Font rasterizer ----------
def rasterize_font(ttf_path, size, chars, glyph_w=None, glyph_h=None, mono_threshold=128):
    """
    Renders each char in `chars` using Pillow into a mono bitmap glyph of width glyph_w x glyph_h.
    If glyph_w/glyph_h are None we auto-size to font ascent/descent bounding box.
    Returns dict: {char: [rows as bytes LSB top], width, height}
    """
    font = ImageFont.truetype(str(ttf_path), size)
    # compute max bbox
    max_w = 0
    max_h = 0
    glyphs = {}
    for ch in chars:
        bbox = font.getbbox(ch)  # (x0, y0, x1, y1)
        w = bbox[2] - bbox[0]
        h = bbox[3] - bbox[1]
        if w > max_w: max_w = w
        if h > max_h: max_h = h
    if glyph_w is None: glyph_w = max(1, max_w)
    if glyph_h is None: glyph_h = max(1, max_h)
    for ch in chars:
        # render onto an image
        img = Image.new('L', (glyph_w, glyph_h), 0)
        draw = ImageDraw.Draw(img)
        # center baseline-ish: try baseline anchor at (0,0)
        draw.text((0, 0), ch, font=font, fill=255)
        # convert to bitmap rows, LSB top per byte column representation similar to earlier font
        # store as bytes per column (like 5x7 font), bit0 top
        cols = []
        for x in range(glyph_w):
            col = 0
            for y in range(glyph_h):
                pix = img.getpixel((x, y))
                if pix >= mono_threshold:
                    col |= (1 << y)
            cols.append(col)
        glyphs[ch] = cols
    return glyphs, glyph_w, glyph_h

# ---------- Header writer ----------
def write_header(outpath: Path, arrays_meta, glyph_meta, emit_c=False, progmem_macro="PROGMEM", per_line=12):
    guard = "ARDUINO_TABLES_GENERATED_H"
    lines = []
    lines.append("// Auto-generated by generate_tables_and_font.py")
    lines.append(f"#ifndef {guard}")
    lines.append(f"#define {guard}\n")
    lines.append("#include <stdint.h>")
    lines.append("#ifdef ARDUINO\n#include <pgmspace.h>\n#endif\n")
    # arrays
    for meta in arrays_meta:
        name = meta["name"]
        ctype = meta["ctype"]
        vals = meta["values"]
        if emit_c:
            lines.append(fmt_c_array_extern(ctype, name, len(vals), progmem_macro))
        else:
            lines.append(fmt_c_array(ctype, name, vals, per_line=per_line, progmem_macro=progmem_macro))
    # glyph metadata & bitmaps
    gw = glyph_meta['width']
    gh = glyph_meta['height']
    chars = glyph_meta['chars']
    lines.append(f"// Glyphs: width={gw}, height={gh}, count={len(chars)}")
    lines.append(f"const uint8_t {progmem_macro} GLYPH_WIDTH = {gw};")
    lines.append(f"const uint8_t {progmem_macro} GLYPH_HEIGHT = {gh};")
    lines.append(f"const uint16_t {progmem_macro} GLYPH_COUNT = {len(chars)};")
    # char map (string)
    char_list = "".join(chars)
    # store char to index mapping: as a string in PROGMEM
    # store glyph bitmaps as columns: for each char, glyph_w bytes (columns), each byte bit0..bitN representing rows
    if emit_c:
        lines.append(f"extern const char {progmem_macro} GLYPH_CHAR_LIST[{len(chars)+1}];\n")
        lines.append(f"extern const uint8_t {progmem_macro} GLYPH_BITMAPS[{len(chars)} * {gw}];\n")
    else:
        # char list
        # show as C string literal
        lines.append(f"const char {progmem_macro} GLYPH_CHAR_LIST[{len(chars)+1}] = \"{char_list}\";")
        # flattened glyph bitmaps
        flat = []
        for ch in chars:
            cols = glyph_meta['glyphs'][ch]
            # ensure length equals gw
            for i in range(gw):
                if i < len(cols):
                    flat.append(cols[i])
                else:
                    flat.append(0)
        lines.append(fmt_c_array("uint8_t", "GLYPH_BITMAPS", flat, per_line=per_line, progmem_macro=progmem_macro))
    # constants
    log_q = next((m for m in arrays_meta if m['name'].startswith('log2_table_q')), None)
    log_q_val = 8
    if log_q:
        # parse name like log2_table_q8
        name = log_q['name']
        try:
            log_q_val = int(name.split('q')[-1])
        except:
            log_q_val = 8
    log_scale = qscale(log_q_val)
    sin_scale = qscale(15) # assume sin table q15
    pi_log = round(math.pi * log_scale)
    two_pi_log = round(2.0 * math.pi * log_scale)
    pi_sinq = round(math.pi * sin_scale)
    two_pi_sinq = round(2.0 * math.pi * sin_scale)
    if emit_c:
        lines.append(f"extern const uint32_t {progmem_macro} CONST_PI_LOG_Q{log_q_val};")
        lines.append(f"extern const uint32_t {progmem_macro} CONST_2PI_LOG_Q{log_q_val};")
        lines.append(f"extern const int32_t {progmem_macro} CONST_PI_SIN_Q15;")
        lines.append(f"extern const int32_t {progmem_macro} CONST_2PI_SIN_Q15;\n")
    else:
        lines.append(f"const uint32_t {progmem_macro} CONST_PI_LOG_Q{log_q_val} = {pi_log};")
        lines.append(f"const uint32_t {progmem_macro} CONST_2PI_LOG_Q{log_q_val} = {two_pi_log};")
        lines.append(f"const int32_t {progmem_macro} CONST_PI_SIN_Q15 = {pi_sinq};")
        lines.append(f"const int32_t {progmem_macro} CONST_2PI_SIN_Q15 = {two_pi_sinq};\n")
    lines.append("#endif // " + guard + "\n")
    outpath.write_text("\n".join(lines))

def write_c_definitions(outpath: Path, arrays_meta, glyph_meta, progmem_macro="PROGMEM", per_line=12):
    lines = []
    header_name = outpath.with_suffix(".h").name
    lines.append("// Auto-generated definitions (generate_tables_and_font.py --emit-c)")
    lines.append(f'#include "{header_name}"\n')
    for meta in arrays_meta:
        lines.append(fmt_c_array(meta['ctype'], meta['name'], meta['values'], per_line=per_line, progmem_macro=progmem_macro))
    # glyphs
    gw = glyph_meta['width']
    gh = glyph_meta['height']
    chars = glyph_meta['chars']
    flat = []
    for ch in chars:
        cols = glyph_meta['glyphs'][ch]
        for i in range(gw):
            flat.append(cols[i] if i < len(cols) else 0)
    lines.append(fmt_c_array("uint8_t", "GLYPH_BITMAPS", flat, per_line=per_line, progmem_macro=progmem_macro))
    # char list and constants
    lines.append(f'const char {progmem_macro} GLYPH_CHAR_LIST[{len(chars)+1}] = "{ "".join(chars) }";\n')
    # constants (use same Q assumptions)
    # try to detect log q
    log_q = next((m for m in arrays_meta if m['name'].startswith('log2_table_q')), None)
    log_q_val = 8
    if log_q:
        try:
            log_q_val = int(log_q['name'].split('q')[-1])
        except:
            log_q_val = 8
    log_scale = qscale(log_q_val)
    sin_scale = qscale(15)
    pi_log = round(math.pi * log_scale)
    two_pi_log = round(2.0 * math.pi * log_scale)
    pi_sinq = round(math.pi * sin_scale)
    two_pi_sinq = round(2.0 * math.pi * sin_scale)
    lines.append(f"const uint32_t {progmem_macro} CONST_PI_LOG_Q{log_q_val} = {pi_log};")
    lines.append(f"const uint32_t {progmem_macro} CONST_2PI_LOG_Q{log_q_val} = {two_pi_log};")
    lines.append(f"const int32_t {progmem_macro} CONST_PI_SIN_Q15 = {pi_sinq};")
    lines.append(f"const int32_t {progmem_macro} CONST_2PI_SIN_Q15 = {two_pi_sinq};\n")
    outpath.with_suffix(".c").write_text("\n".join(lines))

# ---------- CLI ----------
def parse_args():
    p = argparse.ArgumentParser(description="Generate lookup tables and font glyphs for Arduino/ESP32 demos.")
    p.add_argument("--out", "-o", default="arduino_tables", help="Base output file (no extension)")
    p.add_argument("--emit-c", action="store_true", help="Emit .h externs + .c definitions")
    p.add_argument("--font-file", required=True, help="Path to TTF/OTF to rasterize (e.g. Adafruit font or FreeSans)")
    p.add_argument("--font-size", type=int, default=14)
    p.add_argument("--glyph-chars", default="ABCDEFGHIJKLMNOPQRSTUVWXYZ ", help="Characters to rasterize")
    p.add_argument("--glyph-w", type=int, default=None, help="Force glyph width (columns)")
    p.add_argument("--glyph-h", type=int, default=None, help="Force glyph height (rows)")
    p.add_argument("--log-q", type=int, default=8)
    p.add_argument("--sin-cos-size", type=int, default=512)
    return p.parse_args()

def main():
    args = parse_args()
    outbase = Path(args.out)
    # prepare table metas
    arrays_meta = []
    arrays_meta.append({"name":"msb_table", "ctype":"uint8_t", "values":gen_msb_table(256)})
    arrays_meta.append({"name":f"log2_table_q{args.log_q}", "ctype":"uint16_t", "values":gen_log2_table(256, q=args.log_q)})
    arrays_meta.append({"name":f"exp2_table_q{args.log_q}", "ctype":"uint16_t", "values":gen_exp2_frac_table(256, q=args.log_q)})
    sin_tbl, cos_tbl = gen_sin_cos_table(args.sin_cos_size, q=15)
    arrays_meta.append({"name":f"sin_table_q15", "ctype":"int16_t", "values":sin_tbl})
    arrays_meta.append({"name":f"cos_table_q15", "ctype":"int16_t", "values":cos_tbl})
    arrays_meta.append({"name":"perspective_scale_table_q8", "ctype":"uint16_t", "values":gen_perspective_table(256, q=8)})
    arrays_meta.append({"name":"sphere_theta_sin_q15", "ctype":"int16_t", "values":gen_sphere_theta_tables(128, q=15)[0]})
    arrays_meta.append({"name":"sphere_theta_cos_q15", "ctype":"int16_t", "values":gen_sphere_theta_tables(128, q=15)[1]})
    arrays_meta.append({"name":"atan_slope_table_q15", "ctype":"int16_t", "values":gen_atan_table(1024, q=15)})
    arrays_meta.append({"name":"stereo_radial_table_q12", "ctype":"uint16_t", "values":gen_stereographic_table(256, q=12)})

    # glyphs
    glyphs, gw, gh = rasterize_font(args.font_file, args.font_size, list(args.glyph_chars), glyph_w=args.glyph_w, glyph_h=args.glyph_h)
    glyph_meta = {"width": gw, "height": gh, "chars": list(args.glyph_chars), "glyphs": glyphs}

    # write header and optional c file
    header_path = outbase.with_suffix(".h")
    write_header(header_path, arrays_meta, glyph_meta, emit_c=args.emit_c, progmem_macro="PROGMEM")
    if args.emit_c:
        write_c_definitions(outbase.with_suffix(".c"), arrays_meta, glyph_meta, progmem_macro="PROGMEM")
        print(f"Wrote {header_path} and {outbase.with_suffix('.c')}")
    else:
        print(f"Wrote {header_path}")

if __name__ == "__main__":
    main()
