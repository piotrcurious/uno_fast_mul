#!/usr/bin/env python3
"""
generate_ring_float_table_32seg.py

Generate a self-contained header implementing the ring-table approach
for fast uint32 * float on embedded MCUs using 32 segments and a
higher-degree polynomial (default cubic, degree=3) per segment.

Usage:
    python3 generate_ring_float_table_32seg.py --out fast_ring_tables_32.h

Options:
    --out      output header path
    --segments number of segments (default 32)
    --degree   polynomial degree per segment (default 3)
    --samples  samples per segment used for fitting/error (default 8192)
"""
from pathlib import Path
import argparse
import math
import struct
import sys

# -------------------------
# Helpers: float bits
# -------------------------
def float_to_bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]
def bits_to_float(u):
    return struct.unpack("<f", struct.pack("<I", u))[0]

# -------------------------
# Solve small linear system for normal equations (float Gaussian elimination)
# -------------------------
def solve_linear_system(A, b):
    """
    Solve Ax = b in place using Gaussian elimination (A is list of lists).
    Returns solution vector x. A is mutated.
    """
    n = len(b)
    # augmented matrix
    M = [row[:] + [b_i] for row, b_i in zip(A, b)]
    # forward elimination
    for k in range(n):
        # pivot
        pivot = k
        maxval = abs(M[k][k])
        for i in range(k+1, n):
            if abs(M[i][k]) > maxval:
                pivot = i
                maxval = abs(M[i][k])
        if maxval < 1e-30:
            raise RuntimeError("Singular matrix in solve_linear_system")
        if pivot != k:
            M[k], M[pivot] = M[pivot], M[k]
        # normalize pivot row
        piv = M[k][k]
        for j in range(k, n+1):
            M[k][j] /= piv
        # eliminate below
        for i in range(k+1, n):
            fac = M[i][k]
            if fac != 0.0:
                for j in range(k, n+1):
                    M[i][j] -= fac * M[k][j]
    # back substitution
    x = [0.0]*n
    for i in range(n-1, -1, -1):
        x[i] = M[i][n]
        for j in range(i+1, n):
            x[i] -= M[i][j]*x[j]
    return x

# -------------------------
# Least-squares polynomial fit on [0,1) for function y(x)
# degree d: fit y ≈ sum_{k=0..d} a_k x^k
# We'll build normal equations sums directly to avoid large matrices.
# -------------------------
def fit_polynomial_least_squares(y_func, degree, samples):
    """
    y_func(x) for x in [0,1)
    Samples uniform midpoints; returns coefficients [a0..ad]
    """
    d = degree
    n = samples
    # compute moments: S_p = sum x^p, for p up to 2d
    S = [0.0]*(2*d+1)
    T = [0.0]*(d+1)  # T_k = sum y * x^k
    for j in range(n):
        x = (j + 0.5)/n
        xp = 1.0
        y = y_func(x)
        for p in range(2*d+1):
            S[p] += xp
            if p <= d:
                T[p] += y * xp
            xp *= x
    # Build normal matrix A_{ij} = S_{i+j}, rhs = T_i
    A = [[0.0]*(d+1) for _ in range(d+1)]
    for i in range(d+1):
        for j in range(d+1):
            A[i][j] = S[i+j]
    b = T[:]
    coeffs = solve_linear_system(A, b)
    return coeffs

# -------------------------
# Evaluate polynomial error metrics on segment
# -------------------------
def eval_poly_error(coeffs, y_func, samples):
    n = samples
    max_abs = 0.0
    max_rel = 0.0
    sse = 0.0
    d = len(coeffs) - 1
    for j in range(n):
        x = (j + 0.5)/n
        y = y_func(x)
        # Horner evaluate
        acc = 0.0
        for k in range(d, -1, -1):
            acc = acc * x + coeffs[k]
        err = acc - y
        ae = abs(err)
        re = ae / abs(y) if abs(y) > 0 else ae
        if ae > max_abs: max_abs = ae
        if re > max_rel: max_rel = re
        sse += err*err
    rms = math.sqrt(sse / n)
    return max_abs, max_rel, rms

# -------------------------
# Header template
# -------------------------
HEADER_TMPL = r"""/*
  fast_ring_tables_32seg.h
  ------------------------
  Auto-generated ring-table header (Q16.16) for fast uint32 * float.

  segments = {segments}
  degree   = {degree}
  samples  = {samples}

  Runtime:
    - decompose float bits (IEEE754)
    - index table by top_bits of mantissa
    - map fractional mantissa to delta_q16
    - evaluate polynomial in Q16 using Horner:
        m_q16 = a_d
        for k = d-1..0:
            m_q16 = a_k + ((m_q16 * delta_q16) >> 16)
    - result = (a * m_q16) >> 16, then shift by exponent

  Unsigned semantics: negative floats -> 0. Saturation on overflow.
*/

#ifndef FAST_RING_TABLES_32SEG_H
#define FAST_RING_TABLES_32SEG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <math.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {{
#endif

#ifndef RING_MANT_SEGMENTS
#define RING_MANT_SEGMENTS {segments}
#endif

typedef struct {{
    uint32_t a0_q16;
    int32_t  a1_q16;
    int32_t  a2_q16;
    int32_t  a3_q16; /* only present if degree>=3; unused entries zero */
}} RingMantSeg;

static const RingMantSeg RING_MANT_TABLE[RING_MANT_SEGMENTS] = {{
{table_rows}
}};

/* Generator statistics:
{stats}
*/

static inline uint32_t mul_u32_by_q16(uint32_t a, uint32_t mul_q16) {{
    uint64_t prod = (uint64_t)a * (uint64_t)mul_q16;
    return (uint32_t)(prod >> 16);
}}

static inline uint32_t mul_u32_by_float_ring(uint32_t a, float f) {{
    if (a == 0u) return 0u;
    if (!isfinite(f) || f == 0.0f) return 0u;
    if (f < 0.0f) return 0u; // unsigned saturating

    union {{ float f; uint32_t u; }} xu;
    xu.f = f;
    int exp = (int)((xu.u >> 23) & 0xFF) - 127;
    uint32_t mant = (xu.u & 0x7FFFFFu) | 0x800000u; // Q1.23

    const int top_bits = {top_bits}; // log2(segments)
    const int idx = (int)((mant >> (23 - top_bits)) & ((1u << top_bits) - 1));

    const int frac_bits = 23 - top_bits;
    const int shift = frac_bits - 16;
    const uint32_t frac_mask = ((1u << frac_bits) - 1u);
    uint32_t frac = mant & frac_mask;
    uint32_t delta_q16 = (shift >= 0) ? (frac >> shift) : (frac << (-shift));

    RingMantSeg seg = RING_MANT_TABLE[idx];

    // Evaluate polynomial in Q16 via Horner:
    // acc = a_d
    // for k = d-1..0: acc = a_k + ((acc * delta_q16) >> 16)
    int64_t acc = seg.a{deg}_q16; /* start with highest coeff */
{horner_steps}
    uint32_t m_q16 = (uint32_t)acc;

    uint32_t prod = mul_u32_by_q16(a, m_q16);

    if (exp > 0) {{
        if (exp >= 32) return UINT32_MAX;
        uint64_t up = (uint64_t)prod << exp;
        return (up > 0xFFFFFFFFull) ? UINT32_MAX : (uint32_t)up;
    }} else if (exp < 0) {{
        int sh = -exp;
        if (sh >= 32) return 0u;
        return prod >> sh;
    }}
    return prod;
}}

#ifdef __cplusplus
}}
#endif

#endif // FAST_RING_TABLES_32SEG_H
"""

# -------------------------
# Main generation
# -------------------------
def generate(path, segments=32, degree=3, samples_per_segment=8192):
    S = segments
    if (S & (S-1)) != 0:
        print("Warning: segments not power-of-two; indexing logic assumes power-of-two.", file=sys.stderr)

    top_bits = int(math.log2(S))
    if 2**top_bits != S:
        raise ValueError("segments must be power of two for simple bit indexing")

    table_rows = []
    stats_lines = []
    global_max_abs = 0.0
    global_max_rel = 0.0
    global_rms_acc = 0.0
    total_samples = 0

    for i in range(S):
        m_lo = 1.0 + float(i)/S
        m_hi = 1.0 + float(i+1)/S

        # target function y(x): x in [0,1) -> m = m_lo + (m_hi-m_lo)*x
        def y_func(x, lo=m_lo, hi=m_hi):
            return lo + (hi - lo) * x

        # fit polynomial coefficients on x in [0,1)
        coeffs = fit_polynomial_least_squares(y_func, degree, samples_per_segment)
        # ensure length degree+1
        if len(coeffs) < degree+1:
            coeffs += [0.0]*((degree+1)-len(coeffs))

        # convert to Q16.16 (round)
        qcoeffs = [int(round(c*(1<<16))) for c in coeffs]

        # compute errors
        max_abs, max_rel, rms = eval_poly_error(coeffs, y_func, samples_per_segment)
        global_max_abs = max(global_max_abs, max_abs)
        global_max_rel = max(global_max_rel, max_rel)
        global_rms_acc += (rms*rms) * samples_per_segment
        total_samples += samples_per_segment

        # Prepare table row with up to 4 coefficients (pad zeros if degree < 3)
        a0 = qcoeffs[0] & 0xFFFFFFFF
        a1 = qcoeffs[1] if degree >= 1 else 0
        a2 = qcoeffs[2] if degree >= 2 else 0
        a3 = qcoeffs[3] if degree >= 3 else 0

        row = f"    {{ 0x{a0:08X}u, (int32_t)0x{(a1 & 0xFFFFFFFF):08X}, (int32_t)0x{(a2 & 0xFFFFFFFF):08X}, (int32_t)0x{(a3 & 0xFFFFFFFF):08X} }},  /* seg {i}: [{m_lo:.8f},{m_hi:.8f}) err_abs={max_abs:.3e} rel={max_rel:.3e} rms={rms:.3e} */"
        table_rows.append(row)

        stats_lines.append(f"seg {i:2d}: [{m_lo:.6f},{m_hi:.6f}) coeffs = {[round(c,8) for c in coeffs]} q = {[hex(x & 0xFFFFFFFF) for x in qcoeffs]} max_abs={max_abs:.3e} max_rel={max_rel:.3e} rms={rms:.3e}")

    global_rms = math.sqrt(global_rms_acc / total_samples) if total_samples else 0.0
    stats_header = [
        f"Global max abs error (mantissa) = {global_max_abs:.10g}",
        f"Global max rel error (mantissa) = {global_max_rel:.10g}",
        f"Global RMS error (mantissa) = {global_rms:.10g}",
        "",
        "Per-segment summary:"
    ]
    stats = "\n".join(stats_header + ["  " + s for s in stats_lines])

    # prepare horner steps snippet
    # acc = seg.a{deg}_q16
    horner_lines = []
    for k in range(degree-1, -1, -1):
        # acc = a_k + ((acc * delta_q16) >> 16)
        horner_lines.append(f"    acc = (int64_t)seg.a{k}_q16 + ((acc * (int64_t)delta_q16) >> 16);")
    horner_code = "\n".join(horner_lines)

    header_text = HEADER_TMPL.format(
        segments=S,
        degree=degree,
        samples=samples_per_segment,
        table_rows="\n".join(table_rows),
        stats=stats,
        top_bits=top_bits,
        deg=degree,
        horner_steps=horner_code
    )

    Path(path).write_text(header_text)
    print(f"Wrote header: {path}")
    print()
    print("Summary:")
    print(stats_header[0])
    print(stats_header[1])
    print(stats_header[2])
    print("(first 6 per-segment lines):")
    for s in stats_lines[:6]:
        print("  " + s)

# -------------------------
# CLI
# -------------------------
def main():
    p = argparse.ArgumentParser(description="Generate 32-seg high-degree ring-table header")
    p.add_argument("--out", "-o", default="fast_ring_tables_32.h")
    p.add_argument("--segments", type=int, default=32)
    p.add_argument("--degree", type=int, default=3)
    p.add_argument("--samples", type=int, default=8192)
    args = p.parse_args()
    generate(args.out, segments=args.segments, degree=args.degree, samples_per_segment=args.samples)

if __name__ == "__main__":
    main()
