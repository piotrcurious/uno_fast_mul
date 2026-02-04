#!/usr/bin/env python3
"""
generate_tables.py

Generates PROGMEM-friendly C header (and optionally .c) with lookup tables for:
 - fast log/exp-based multiplication (msb, log2, exp2)
 - trig (sin, cos)
 - perspective scale (focal/(focal+z))
 - sphere coordinates (theta sin/cos)
 - base constants (PI, 2PI in chosen Q formats)
 - optional angle (atan-approx) table and stereographic projection table

Uses mathematically correct formulas for all tables.
"""

from pathlib import Path
import math
import argparse

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
    tbl = []
    for i in range(size):
        if i < 1:
            tbl.append(0)
        else:
            v = round(math.log2(i) * scale)
            tbl.append(clamp_int(v, 0, 0xFFFF))
    return tbl

def gen_exp2_frac_table(frac_size=256, q=8):
    scale = qscale(q)
    tbl = []
    for f in range(frac_size):
        v = round((2 ** (f / frac_size)) * scale)
        tbl.append(clamp_int(v, 0, 0xFFFF))
    return tbl

def gen_sin_cos_table(n=512, q=15):
    scale = qscale(q)
    sin_tbl = []
    cos_tbl = []
    for i in range(n):
        angle = 2.0 * math.pi * i / n
        sin_tbl.append(clamp_int(round(math.sin(angle) * scale), -32768, 32767))
        cos_tbl.append(clamp_int(round(math.cos(angle) * scale), -32768, 32767))
    return sin_tbl, cos_tbl

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
    sin_t = []
    cos_t = []
    for t in range(theta_steps):
        theta = math.pi * t / (theta_steps - 1)
        sin_t.append(clamp_int(round(math.sin(theta) * scale), -32768, 32767))
        cos_t.append(clamp_int(round(math.cos(theta) * scale), -32768, 32767))
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
    args = parser.parse_args()

    base = Path(args.out)
    header_path = base.with_suffix(".h")

    arrays = []

    # MSB
    msb = gen_msb_table(args.msb_size)
    arrays.append(("uint8_t", "msb_table", msb))

    # Log2
    log2 = gen_log2_table(args.log_size, q=args.log_q)
    arrays.append(("uint16_t", f"log2_table_q{args.log_q}", log2))

    # Exp2
    exp2 = gen_exp2_frac_table(args.exp_frac_size, q=args.log_q)
    arrays.append(("uint16_t", f"exp2_table_q{args.log_q}", exp2))

    # Sin/Cos
    sin_tbl, cos_tbl = gen_sin_cos_table(args.sin_cos_size, q=args.sin_cos_q)
    arrays.append(("int16_t", f"sin_table_q{args.sin_cos_q}", sin_tbl))
    arrays.append(("int16_t", f"cos_table_q{args.sin_cos_q}", cos_tbl))

    # Perspective
    persp = gen_perspective_table(args.persp_size, q=args.persp_q, focal=args.persp_focal, zmin=args.persp_zmin, zmax=args.persp_zmax)
    persp_type = "uint16_t" if max(persp) <= 0xFFFF else "uint32_t"
    arrays.append((persp_type, f"perspective_scale_table_q{args.persp_q}", persp))

    # Sphere
    s_sin, s_cos = gen_sphere_theta_tables(args.sphere_theta_steps, q=args.sphere_q)
    arrays.append(("int16_t", f"sphere_theta_sin_q{args.sphere_q}", s_sin))
    arrays.append(("int16_t", f"sphere_theta_cos_q{args.sphere_q}", s_cos))

    # Atan
    if args.gen_atan:
        atan_tbl = gen_atan_table(args.atan_size, q=args.atan_q, x_range=args.atan_range)
        arrays.append(("int16_t", f"atan_slope_table_q{args.atan_q}", atan_tbl))

    # Stereo
    if args.gen_stereo:
        stereo = gen_stereographic_table(args.stereo_size, q=args.stereo_q)
        stereo_type = "uint16_t" if max(stereo) <= 0xFFFF else "uint32_t"
        arrays.append((stereo_type, f"stereo_radial_table_q{args.stereo_q}", stereo))

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
    h_content = [f"#ifndef {guard}", f"#define {guard}", '#include <stdint.h>', '#include <avr/pgmspace.h>\n']

    if args.emit_c:
        for ctype, name, vals in arrays:
            h_content.append(f"extern const {ctype} {args.progmem_macro} {name}[{len(vals)}];")
        h_content.append("")
        for ctype, name, val in constants:
            h_content.append(f"extern const {ctype} {args.progmem_macro} {name};")
        h_content.append(f"\n#endif")
        header_path.write_text("\n".join(h_content))

        c_content = [f'#include "{header_path.name}"\n']
        for ctype, name, vals in arrays:
            c_content.append(fmt_c_array(ctype, name, vals, progmem_macro=args.progmem_macro))
        c_content.append("")
        for ctype, name, val in constants:
            c_content.append(f"const {ctype} {args.progmem_macro} {name} = {val};")
        base.with_suffix(".c").write_text("\n".join(c_content))
    else:
        for ctype, name, vals in arrays:
            h_content.append(fmt_c_array(ctype, name, vals, progmem_macro=args.progmem_macro))
        h_content.append("")
        for ctype, name, val in constants:
            h_content.append(f"const {ctype} {args.progmem_macro} {name} = {val};")
        h_content.append(f"\n#endif")
        header_path.write_text("\n".join(h_content))

if __name__ == "__main__":
    main()
