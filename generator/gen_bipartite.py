import math
import argparse
from pathlib import Path

def qscale(q): return 1 << q

def gen_btm_log2(n1, n2, n3):
    # f(x) = log2(1+x) for x in [0, 1)
    def f(x): return math.log2(1 + x)

    t1_size = 2**(n1 + n2)
    t2_size = 2**(n1 + n3)
    t1 = [0] * t1_size
    t2 = [0] * t2_size

    scale = 65536 # Q16

    for i1 in range(2**n1):
        for i2 in range(2**n2):
            x_val = (i1 * 2**n2 + i2) * 2**n3 + 2**(n3-1)
            x = x_val / 2**(n1+n2+n3)
            t1[i1 * 2**n2 + i2] = round(f(x) * scale)

    for i1 in range(2**n1):
        x_start = (i1 * 2**(n2+n3)) / 2**(n1+n2+n3)
        x_end = ((i1+1) * 2**(n2+n3)) / 2**(n1+n2+n3)
        slope = (f(x_end) - f(x_start)) / (2**(n2+n3) / 2**(n1+n2+n3))

        for i3 in range(2**n3):
            correction = slope * (i3 - 2**(n3-1)) / 2**(n1+n2+n3)
            t2[i1 * 2**n3 + i3] = round(correction * scale)

    return t1, t2

def gen_btm_exp2(n1, n2, n3):
    # f(x) = 2^x - 1 for x in [0, 1)
    # We want the fractional part of 2^x
    def f(x): return 2**x - 1

    t1_size = 2**(n1 + n2)
    t2_size = 2**(n1 + n3)
    t1 = [0] * t1_size
    t2 = [0] * t2_size

    scale = 65536 # Q16

    for i1 in range(2**n1):
        for i2 in range(2**n2):
            x_val = (i1 * 2**n2 + i2) * 2**n3 + 2**(n3-1)
            x = x_val / 2**(n1+n2+n3)
            t1[i1 * 2**n2 + i2] = round(f(x) * scale)

    for i1 in range(2**n1):
        x_start = (i1 * 2**(n2+n3)) / 2**(n1+n2+n3)
        x_end = ((i1+1) * 2**(n2+n3)) / 2**(n1+n2+n3)
        slope = (f(x_end) - f(x_start)) / (2**(n2+n3) / 2**(n1+n2+n3))

        for i3 in range(2**n3):
            correction = slope * (i3 - 2**(n3-1)) / 2**(n1+n2+n3)
            t2[i1 * 2**n3 + i3] = round(correction * scale)

    return t1, t2

def fmt_c_array(ctype, name, values, per_line=8):
    lines = []
    for i in range(0, len(values), per_line):
        chunk = values[i:i+per_line]
        lines.append("  " + ", ".join(str(int(v)) for v in chunk) + ("," if i+per_line < len(values) else ""))
    body = "\n".join(lines)
    return f"const {ctype} PROGMEM {name}[{len(values)}] = {{\n{body}\n}};\n"

def main():
    n1, n2, n3 = 4, 5, 5
    l_t1, l_t2 = gen_btm_log2(n1, n2, n3)
    e_t1, e_t2 = gen_btm_exp2(n1, n2, n3)

    with open("fast_float_tables.c", "w") as f:
        f.write('#include "fast_float.h"\n\n')
        f.write(fmt_c_array("uint16_t", "log2_t1", l_t1))
        f.write(fmt_c_array("int16_t", "log2_t2", l_t2))
        f.write(fmt_c_array("uint16_t", "exp2_t1", e_t1))
        f.write(fmt_c_array("int16_t", "exp2_t2", e_t2))

if __name__ == "__main__":
    main()
