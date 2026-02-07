#!/usr/bin/env python3
"""
generate_tables.py

Generates PROGMEM-friendly C header (and optionally .cpp) with lookup tables for:
 - fast log/exp-based multiplication (msb, log2, exp2) - both fixed-point and BTM float
 - trig (sin, cos)
 - perspective scale (focal/(focal+z))
 - sphere coordinates (theta sin/cos)
 - base constants (PI, 2PI in chosen Q formats)
 - optional angle (atan-approx) table and stereographic projection table
 - optional glyph bitmaps for TTF/OTF fonts (requires Pillow)

Uses mathematically correct formulas for all tables.
"""

from pathlib import Path
import math
import argparse

try:
    from PIL import Image, ImageFont, ImageDraw
    PILLOW_INSTALLED = True
except ImportError:
    PILLOW_INSTALLED = False

def qscale(q): return 1 << q
def clamp_int(x, lo, hi): return max(lo, min(hi, int(x)))

def fmt_c_array(ctype, name, values, per_line=8, progmem_macro="PROGMEM"):
    lines = []
    for i in range(0, len(values), per_line):
        chunk = values[i:i+per_line]
        lines.append("  " + ", ".join(str(int(v)) for v in chunk) + ("," if i+per_line < len(values) else ""))
    body = "\n".join(lines)
    return f"const {ctype} {progmem_macro} {name}[{len(values)}] = {{\n{body}\n}};\n"

def gen_msb_table(size=256):
    return [0 if i == 0 else int(math.floor(math.log2(i))) for i in range(size)]

def gen_log2_table(size=256, q=8):
    scale = qscale(q)
    return [round(math.log2(i) * scale) if i >= 1 else 0 for i in range(size)]

def gen_exp2_frac_table(frac_size=256, q=8):
    scale = qscale(q)
    return [round((2 ** (f / frac_size)) * scale) for f in range(frac_size)]

def gen_btm_log2(n1, n2, n3):
    def f(x): return math.log2(1 + x)
    t1 = []
    for i12 in range(2**(n1+n2)):
        x_val = (i12 * 2**n3) + 2**(n3-1)
        x = x_val / 2**(n1+n2+n3)
        t1.append(round(f(x) * 65536))
    t2 = []
    for i13 in range(2**(n1+n3)):
        i1 = i13 >> n3
        x_start = (i1 * 2**(n2+n3)) / 2**(n1+n2+n3)
        x_end = ((i1+1) * 2**(n2+n3)) / 2**(n1+n2+n3)
        slope = (f(x_end) - f(x_start)) / (2**(n2+n3) / 2**(n1+n2+n3))
        correction = slope * (i13 % 2**n3 - 2**(n3-1)) / 2**(n1+n2+n3)
        t2.append(round(correction * 65536))
    return t1, t2

def gen_btm_exp2(n1, n2, n3):
    def f(x): return 2**x - 1
    t1 = []
    for i12 in range(2**(n1+n2)):
        x_val = (i12 * 2**n3) + 2**(n3-1)
        x = x_val / 2**(n1+n2+n3)
        t1.append(round(f(x) * 65536))
    t2 = []
    for i13 in range(2**(n1+n3)):
        i1 = i13 >> n3
        x_start = (i1 * 2**(n2+n3)) / 2**(n1+n2+n3)
        x_end = ((i1+1) * 2**(n2+n3)) / 2**(n1+n2+n3)
        slope = (f(x_end) - f(x_start)) / (2**(n2+n3) / 2**(n1+n2+n3))
        correction = slope * (i13 % 2**n3 - 2**(n3-1)) / 2**(n1+n2+n3)
        t2.append(round(correction * 65536))
    return t1, t2

def gen_sin_cos_table(n=512, q=15):
    scale = qscale(q)
    sin_tbl = [clamp_int(round(math.sin(2.0*math.pi*i/n)*scale), -32768, 32767) for i in range(n)]
    cos_tbl = [clamp_int(round(math.cos(2.0*math.pi*i/n)*scale), -32768, 32767) for i in range(n)]
    return sin_tbl, cos_tbl

def gen_log_sin_cos_table(n=512, q=8):
    # stores log2(|sin(x)| * 2^15) in Q8.8?
    # Better: stores log2(|sin(x)|) + 16.0 such that exp2 gives Q16.16
    # sin(x) range [0, 1]. log2(sin(x)) range [-inf, 0].
    # log2(sin(x)) + 16 range [-inf, 16].
    scale = qscale(q)
    lsin = []
    lcos = []
    for i in range(n):
        s = abs(math.sin(2.0*math.pi*i/n))
        if s < 1e-9: lsin.append(-32768)
        else: lsin.append(round((math.log2(s) + 16.0) * scale))

        c = abs(math.cos(2.0*math.pi*i/n))
        if c < 1e-9: lcos.append(-32768)
        else: lcos.append(round((math.log2(c) + 16.0) * scale))
    return lsin, lcos

def gen_perspective_table(n=256, q=8, focal=256.0, zmin=0.0, zmax=1024.0):
    scale = qscale(q)
    tbl = []
    for i in range(n):
        z = zmin + (zmax - zmin) * (i / (n - 1))
        denom = focal + z
        s = (focal / denom) if denom != 0.0 else 0.0
        tbl.append(clamp_int(round(s * scale), 0, 0xFFFFFFFF))
    return tbl

def gen_sphere_theta_tables(theta_steps=128, q=15):
    scale = qscale(q)
    sin_t = [clamp_int(round(math.sin(math.pi*t/(theta_steps-1))*scale), -32768, 32767) for t in range(theta_steps)]
    cos_t = [clamp_int(round(math.cos(math.pi*t/(theta_steps-1))*scale), -32768, 32767) for t in range(theta_steps)]
    return sin_t, cos_t

def gen_atan_table(n=1024, q=15, x_range=4.0):
    scale = qscale(q)
    return [clamp_int(round(math.atan(((i/(n-1))*2.0*x_range)-x_range)*scale), -32768, 32767) for i in range(n)]

def gen_atan_q15_table(n=256):
    # atan(x) for x in [0, 1], result in Q15 where 2*PI = 65536
    # so atan(1) = PI/4 = 65536 / 8 = 8192
    tbl = []
    for i in range(n):
        x = i / (n - 1)
        angle = math.atan(x)
        # map [0, 2*PI] to [0, 65536]
        val = (angle / (2 * math.pi)) * 65536
        tbl.append(round(val))
    return tbl

def gen_acos_table(n=256):
    # acos(x) for x in [0, 1], result in Q15
    tbl = []
    for i in range(n):
        x = i / (n - 1)
        angle = math.acos(x)
        val = (angle / (2 * math.pi)) * 65536
        tbl.append(round(val))
    return tbl

def gen_stereographic_table(n=256, q=12):
    scale = qscale(q)
    return [round((2.0 / (1.0 + ((i/(n-1))*2.0)**2)) * scale) for i in range(n)]

def gen_lse_table(n=256, q=8, x_range=8.0):
    # f(x) = log2(1 + 2^-x) for x in [0, x_range]
    # We use -x because for LogSumExp(a, b) = max(a,b) + log2(1 + 2^-|a-b|)
    scale = qscale(q)
    tbl = []
    for i in range(n):
        x = x_range * i / (n - 1)
        val = math.log2(1 + 2**(-x))
        tbl.append(round(val * scale))
    return tbl

def rasterize_font(ttf_path, size, chars, glyph_w=None, glyph_h=None, mono_threshold=128):
    if not PILLOW_INSTALLED:
        raise ImportError("Pillow is required for font rasterization. Install it with 'pip install pillow'.")
    font = ImageFont.truetype(str(ttf_path), size)
    max_w, max_h = 0, 0
    glyphs = {}
    for ch in chars:
        bbox = font.getbbox(ch)
        w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
        max_w, max_h = max(max_w, w), max(max_h, h)
    if glyph_w is None: glyph_w = max(1, max_w)
    if glyph_h is None: glyph_h = max(1, max_h)
    for ch in chars:
        img = Image.new('L', (glyph_w, glyph_h), 0)
        draw = ImageDraw.Draw(img)
        draw.text((0, 0), ch, font=font, fill=255)
        cols = []
        for x in range(glyph_w):
            col = 0
            for y in range(glyph_h):
                if img.getpixel((x, y)) >= mono_threshold:
                    col |= (1 << y)
            cols.append(col)
        glyphs[ch] = cols
    return glyphs, glyph_w, glyph_h

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", "-o", default="arduino_tables_generated")
    parser.add_argument("--emit-c", action="store_true")
    parser.add_argument("--progmem-macro", default="PROGMEM")
    parser.add_argument("--log-q", type=int, default=8)
    parser.add_argument("--msb-size", type=int, default=256)
    parser.add_argument("--log-size", type=int, default=256)
    parser.add_argument("--exp-frac-size", type=int, default=256)
    parser.add_argument("--sin-cos-size", type=int, default=512)
    parser.add_argument("--sin-cos-q", type=int, default=15)
    parser.add_argument("--persp-size", type=int, default=256)
    parser.add_argument("--persp-q", type=int, default=8)
    parser.add_argument("--persp-focal", type=float, default=256.0)
    parser.add_argument("--persp-zmin", type=float, default=0.0)
    parser.add_argument("--persp-zmax", type=float, default=1024.0)
    parser.add_argument("--sphere-theta-steps", type=int, default=128)
    parser.add_argument("--sphere-q", type=int, default=15)
    parser.add_argument("--gen-atan", action="store_true")
    parser.add_argument("--atan-size", type=int, default=1024)
    parser.add_argument("--atan-q", type=int, default=15)
    parser.add_argument("--atan-range", type=float, default=4.0)
    parser.add_argument("--gen-stereo", action="store_true")
    parser.add_argument("--stereo-size", type=int, default=256)
    parser.add_argument("--stereo-q", type=int, default=12)
    parser.add_argument("--gen-float", action="store_true", help="Generate BTM float tables")
    parser.add_argument("--font-file", help="Path to TTF/OTF font file for rasterization")
    parser.add_argument("--font-size", type=int, default=14)
    parser.add_argument("--glyph-chars", default=" !?0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ")
    parser.add_argument("--glyph-w", type=int, default=None)
    parser.add_argument("--glyph-h", type=int, default=None)
    parser.add_argument("--gen-lse", action="store_true", help="Generate LogSumExp table")
    parser.add_argument("--gen-log-trig", action="store_true", help="Generate Log-domain sin/cos tables")
    args = parser.parse_args()

    base = Path(args.out)
    header_path = base.with_suffix(".h")
    arrays = []

    msb = gen_msb_table(args.msb_size)
    arrays.append(("uint8_t", "msb_table", msb))
    log2 = gen_log2_table(args.log_size, q=args.log_q)
    arrays.append(("uint16_t", f"log2_table_q{args.log_q}", log2))
    exp2 = gen_exp2_frac_table(args.exp_frac_size, q=args.log_q)
    arrays.append(("uint16_t", f"exp2_table_q{args.log_q}", exp2))

    sin_tbl, cos_tbl = gen_sin_cos_table(args.sin_cos_size, q=args.sin_cos_q)
    arrays.append(("int16_t", f"sin_table_q{args.sin_cos_q}", sin_tbl))
    arrays.append(("int16_t", f"cos_table_q{args.sin_cos_q}", cos_tbl))

    if args.gen_log_trig:
        lsin, lcos = gen_log_sin_cos_table(args.sin_cos_size, q=args.log_q)
        arrays.append(("int16_t", f"log_sin_table_q{args.log_q}", lsin))
        arrays.append(("int16_t", f"log_cos_table_q{args.log_q}", lcos))

    persp = gen_perspective_table(args.persp_size, q=args.persp_q, focal=args.persp_focal, zmin=args.persp_zmin, zmax=args.persp_zmax)
    persp_type = "uint16_t" if max(persp) <= 0xFFFF else "uint32_t"
    arrays.append((persp_type, f"perspective_scale_table_q{args.persp_q}", persp))

    s_sin, s_cos = gen_sphere_theta_tables(args.sphere_theta_steps, q=args.sphere_q)
    arrays.append(("int16_t", f"sphere_theta_sin_q{args.sphere_q}", s_sin))
    arrays.append(("int16_t", f"sphere_theta_cos_q{args.sphere_q}", s_cos))

    if args.gen_atan:
        arrays.append(("int16_t", f"atan_slope_table_q{args.atan_q}", gen_atan_table(args.atan_size, q=args.atan_q, x_range=args.atan_range)))
        arrays.append(("uint16_t", "atan_q15_table", gen_atan_q15_table(256)))
        arrays.append(("uint16_t", "acos_table", gen_acos_table(256)))
    if args.gen_stereo:
        stereo = gen_stereographic_table(args.stereo_size, q=args.stereo_q)
        arrays.append(("uint16_t" if max(stereo) <= 0xFFFF else "uint32_t", f"stereo_radial_table_q{args.stereo_q}", stereo))

    if args.gen_lse:
        lse = gen_lse_table(256, q=args.log_q)
        arrays.append(("uint16_t", "lse_table_q8", lse))

    if args.gen_float:
        l_t1, l_t2 = gen_btm_log2(4, 5, 5)
        e_t1, e_t2 = gen_btm_exp2(4, 5, 5)
        arrays.append(("uint16_t", "log2_t1", l_t1))
        arrays.append(("int16_t", "log2_t2", l_t2))
        arrays.append(("uint16_t", "exp2_t1", e_t1))
        arrays.append(("int16_t", "exp2_t2", e_t2))

    glyph_meta = None
    if args.font_file:
        glyphs, gw, gh = rasterize_font(args.font_file, args.font_size, list(args.glyph_chars), glyph_w=args.glyph_w, glyph_h=args.glyph_h)
        glyph_meta = {"width": gw, "height": gh, "chars": list(args.glyph_chars), "glyphs": glyphs}

    # constants
    log_scale = qscale(args.log_q)
    sin_scale = qscale(args.sin_cos_q)
    pi_log = round(math.pi * log_scale)
    two_pi_log = round(2.0 * math.pi * log_scale)
    pi_sinq = round(math.pi * sin_scale)
    two_pi_sinq = round(2.0 * math.pi * sin_scale)

    constants = [
        ("uint32_t", f"CONST_PI_LOG_Q{args.log_q}", pi_log),
        ("uint32_t", f"CONST_2PI_LOG_Q{args.log_q}", two_pi_log),
        ("int32_t", f"CONST_PI_SIN_Q{args.sin_cos_q}", pi_sinq),
        ("int32_t", f"CONST_2PI_SIN_Q{args.sin_cos_q}", two_pi_sinq),
    ]

    guard = base.name.upper() + "_H"
    h_content = [f"#ifndef {guard}", f"#define {guard}", '#include <stdint.h>', '#ifdef ARDUINO', '#include <avr/pgmspace.h>', '#else', '#ifndef PROGMEM', '#define PROGMEM', '#endif', '#endif\n']

    if args.emit_c:
        for ctype, name, vals in arrays:
            h_content.append(f"extern const {ctype} {args.progmem_macro} {name}[{len(vals)}];")
            h_content.append(f"#define {name.upper()}_SIZE {len(vals)}")
        if glyph_meta:
            gh = glyph_meta['height']
            glyph_type = "uint8_t"
            if gh > 16: glyph_type = "uint32_t"
            elif gh > 8: glyph_type = "uint16_t"
            h_content.append(f"extern const uint8_t {args.progmem_macro} GLYPH_WIDTH;")
            h_content.append(f"extern const uint8_t {args.progmem_macro} GLYPH_HEIGHT;")
            h_content.append(f"extern const uint16_t {args.progmem_macro} GLYPH_COUNT;")
            h_content.append(f"extern const char {args.progmem_macro} GLYPH_CHAR_LIST[{len(glyph_meta['chars'])+1}];")
            h_content.append(f"extern const {glyph_type} {args.progmem_macro} GLYPH_BITMAPS[{len(glyph_meta['chars']) * glyph_meta['width']}];")
        h_content.append("")
        for ctype, name, val in constants:
            h_content.append(f"extern const {ctype} {args.progmem_macro} {name};")
        h_content.append(f"\n#endif")
        header_path.write_text("\n".join(h_content))

        c_content = [f'#include "{header_path.name}"\n']
        for ctype, name, vals in arrays:
            c_content.append(fmt_c_array(ctype, name, vals, progmem_macro=args.progmem_macro))
        if glyph_meta:
            gh = glyph_meta['height']
            glyph_type = "uint8_t"
            if gh > 16: glyph_type = "uint32_t"
            elif gh > 8: glyph_type = "uint16_t"
            c_content.append(f"const uint8_t {args.progmem_macro} GLYPH_WIDTH = {glyph_meta['width']};")
            c_content.append(f"const uint8_t {args.progmem_macro} GLYPH_HEIGHT = {glyph_meta['height']};")
            c_content.append(f"const uint16_t {args.progmem_macro} GLYPH_COUNT = {len(glyph_meta['chars'])};")
            c_content.append(f'const char {args.progmem_macro} GLYPH_CHAR_LIST[{len(glyph_meta["chars"])+1}] = "{ "".join(glyph_meta["chars"]) }";')
            flat_glyphs = []
            for ch in glyph_meta['chars']:
                cols = glyph_meta['glyphs'][ch]
                flat_glyphs.extend(cols + [0] * (glyph_meta['width'] - len(cols)))
            c_content.append(fmt_c_array(glyph_type, "GLYPH_BITMAPS", flat_glyphs, progmem_macro=args.progmem_macro))
        c_content.append("")
        for ctype, name, val in constants:
            c_content.append(f"const {ctype} {args.progmem_macro} {name} = {val};")
        base.with_suffix(".cpp").write_text("\n".join(c_content))
    else:
        for ctype, name, vals in arrays:
            h_content.append(f"#define {name.upper()}_SIZE {len(vals)}")
            h_content.append(fmt_c_array(ctype, name, vals, progmem_macro=args.progmem_macro))
        if glyph_meta:
            gh = glyph_meta['height']
            glyph_type = "uint8_t"
            if gh > 16: glyph_type = "uint32_t"
            elif gh > 8: glyph_type = "uint16_t"
            h_content.append(f"const uint8_t {args.progmem_macro} GLYPH_WIDTH = {glyph_meta['width']};")
            h_content.append(f"const uint8_t {args.progmem_macro} GLYPH_HEIGHT = {glyph_meta['height']};")
            h_content.append(f"const uint16_t {args.progmem_macro} GLYPH_COUNT = {len(glyph_meta['chars'])};")
            h_content.append(f'const char {args.progmem_macro} GLYPH_CHAR_LIST[{len(glyph_meta["chars"])+1}] = "{ "".join(glyph_meta["chars"]) }";')
            flat_glyphs = []
            for ch in glyph_meta['chars']:
                cols = glyph_meta['glyphs'][ch]
                flat_glyphs.extend(cols + [0] * (glyph_meta['width'] - len(cols)))
            h_content.append(fmt_c_array(glyph_type, "GLYPH_BITMAPS", flat_glyphs, progmem_macro=args.progmem_macro))
        h_content.append("")
        for ctype, name, val in constants:
            h_content.append(f"const {ctype} {args.progmem_macro} {name} = {val};")
        h_content.append(f"\n#endif")
        header_path.write_text("\n".join(h_content))

if __name__ == "__main__":
    main()
