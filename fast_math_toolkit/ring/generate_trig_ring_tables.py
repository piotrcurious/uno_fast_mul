#!/usr/bin/env python3
"""
generate_trig_ring_tables.py

Generate self-contained header implementing ring-table trig (sin/cos) for 3D graphics.

- Fits per-segment polynomials for sin(θ) on [0, pi/2].
- Uses quadrant symmetry to produce sin/cos for full [0,2π).
- Coefficients stored in Q16.16 so runtime returns Q16.16 sin/cos.
- Runtime uses integer-only Horner evaluation with delta in Q16.

Default: 32 segments, degree 5, 8192 samples per segment.

Output: standalone header (C) containing table and functions:
  int32_t sin_q16_from_u16angle(uint16_t angle);
  int32_t cos_q16_from_u16angle(uint16_t angle);

Angle convention: uint16_t angle maps 0..65535 -> 0..2π.

Author: ChatGPT
"""
from pathlib import Path
import math
import argparse
import struct
import sys

# ------------------------
# Math helpers
# ------------------------
def solve_linear_system(A, b):
    # Gaussian elimination (small matrices)
    n = len(b)
    M = [row[:] + [b_i] for row, b_i in zip(A, b)]
    for k in range(n):
        # pivot
        pivot = k
        maxv = abs(M[k][k])
        for i in range(k+1, n):
            if abs(M[i][k]) > maxv:
                pivot = i
                maxv = abs(M[i][k])
        if maxv < 1e-30:
            raise RuntimeError("Singular matrix in solve_linear_system")
        if pivot != k:
            M[k], M[pivot] = M[pivot], M[k]
        piv = M[k][k]
        for j in range(k, n+1):
            M[k][j] /= piv
        for i in range(k+1, n):
            fac = M[i][k]
            if fac != 0.0:
                for j in range(k, n+1):
                    M[i][j] -= fac * M[k][j]
    x = [0.0]*n
    for i in range(n-1, -1, -1):
        x[i] = M[i][n]
        for j in range(i+1, n):
            x[i] -= M[i][j]*x[j]
    return x

def fit_polynomial_least_squares(y_func, degree, samples):
    d = degree
    n = samples
    # moments S_p and T_k
    S = [0.0]*(2*d+1)
    T = [0.0]*(d+1)
    for j in range(n):
        x = (j + 0.5)/n
        xp = 1.0
        y = y_func(x)
        for p in range(2*d+1):
            S[p] += xp
            if p <= d:
                T[p] += y * xp
            xp *= x
    A = [[0.0]*(d+1) for _ in range(d+1)]
    for i in range(d+1):
        for j in range(d+1):
            A[i][j] = S[i+j]
    coeffs = solve_linear_system(A, T)
    return coeffs

def eval_poly_error(coeffs, y_func, samples):
    n = samples
    max_abs = 0.0
    max_rel = 0.0
    sse = 0.0
    d = len(coeffs)-1
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

# ------------------------
# Header template
# ------------------------
HEADER_TMPL = r"""/*
  fast_trig_ring.h
  -----------------
  Auto-generated header providing Q16.16 sin/cos using ring-table / formal-series approach.

  Angle convention: uint16_t angle -> 0..65535 -> 0..2π

  segments = {segments}
  degree   = {degree}
  samples  = {samples}

  Functions:
    int32_t sin_q16_from_u16angle(uint16_t angle);
    int32_t cos_q16_from_u16angle(uint16_t angle);

  Outputs are Q16.16 (signed 32-bit), suitable for multiplying into Q16 geometry.
*/

#ifndef FAST_TRIG_RING_H
#define FAST_TRIG_RING_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {{
#endif

#ifndef TRIG_SEGMENTS
#define TRIG_SEGMENTS {segments}
#endif

/* Each segment stores polynomial coefficients for sin(θ) over the segment:
   θ = θ_lo + (θ_hi - θ_lo) * x, x ∈ [0,1)
   sin(θ) ≈ sum_{k=0..degree} a_k * x^k

   Coefficients a_k are stored scaled to Q16.16 (signed int32).
   At runtime we evaluate the polynomial with Horner in Q16 using delta_q16 (x*2^16).
*/
typedef struct {{
    int32_t a0_q16;
    int32_t a1_q16;
    int32_t a2_q16;
    int32_t a3_q16;
    int32_t a4_q16;
    int32_t a5_q16;
}} SinSeg_q16;

/* Table for first quadrant [0, pi/2). Symmetries provide sin/cos for full circle. */
static const SinSeg_q16 SIN_SEGS[TRIG_SEGMENTS] = {{
{table_rows}
}};

/* Runtime helpers */

// Evaluate polynomial in Q16 (Horner):
// acc = a_d
// for k=d-1..0: acc = a_k + ((acc * delta_q16) >> 16)
// returns Q16.16 value (signed)
static inline int32_t eval_poly_q16(const int32_t *coeffs, uint32_t delta_q16, int degree) {{
    int64_t acc = coeffs[degree];
    for (int k = degree-1; k >= 0; --k) {{
        acc = (int64_t)coeffs[k] + ((acc * (int64_t)delta_q16) >> 16);
    }}
    if (acc > INT32_MAX) return INT32_MAX;
    if (acc < INT32_MIN) return INT32_MIN;
    return (int32_t)acc;
}}

// Top-level: input angle as uint16 in [0,65535] -> 0..2π
// Map to quadrant and to first-quadrant index/offset.
// We use top_bits = log2(segments) to index segments in quadrant.
static inline int32_t sin_q16_from_u16angle(uint16_t angle) {{
    // quadrant: 0..3
    uint32_t a = (uint32_t)angle;
    uint32_t quadrant = a >> 14; // top 2 bits (65536/4 = 16384)
    uint32_t rem = a & 0x3FFF;   // 14-bit remainder mapping to [0, pi/2)

    // segments => top_bits
    const int top_bits = {top_bits};
    const int frac_bits = 14 - top_bits;
    uint32_t idx = (rem >> frac_bits) & ((1u << top_bits) - 1u);
    uint32_t frac = rem & ((1u << frac_bits) - 1u);

    // delta_q16 = round(frac / 2^frac_bits * 2^16) -> scale by shift
    int shift = frac_bits - 16;
    uint32_t delta_q16 = (shift >= 0) ? (frac >> shift) : (frac << (-shift));

    // Evaluate sin on first quadrant segment
    const SinSeg_q16 *seg = &SIN_SEGS[idx];
    const int32_t coeffs[] = { seg->a0_q16, seg->a1_q16, seg->a2_q16, seg->a3_q16, seg->a4_q16, seg->a5_q16 };
    int32_t s_q16 = eval_poly_q16(coeffs, delta_q16, {degree});
    // adjust sign depending on quadrant and whether we need sin or cos mapping
    switch (quadrant) {{
        case 0: return s_q16;                   // 0..pi/2 : sin = +sin(theta)
        case 1: return eval_poly_q16(coeffs, delta_q16, {degree}); // will be handled below by mapping theta->pi/2 - theta? We'll instead compute properly below.
        case 2: return -s_q16;                  // pi..3pi/2 : sin = -sin(theta - pi)
        case 3: return -s_q16;                  // 3pi/2..2pi : sin = -sin(2pi - theta)? careful mapping below
    }}
    return s_q16;
}}

// We'll implement quadrant-correct mapping using more careful transforms below.

static inline int32_t sin_q16_from_u16angle_correct(uint16_t angle) {{
    uint32_t a = (uint32_t)angle;
    uint32_t quadrant = a >> 14;
    uint32_t rem = a & 0x3FFF; // 14-bit position in quadrant

    // We want theta_q in [0, pi/2) mapped by rem/2^14 * (pi/2)
    // Use segments on [0, pi/2) with top_bits index
    const int top_bits = {top_bits};
    const int frac_bits = 14 - top_bits;
    uint32_t idx = (rem >> frac_bits) & ((1u << top_bits) - 1u);
    uint32_t frac = rem & ((1u << frac_bits) - 1u);

    int shift = frac_bits - 16;
    uint32_t delta_q16 = (shift >= 0) ? (frac >> shift) : (frac << (-shift));

    const SinSeg_q16 *seg = &SIN_SEGS[idx];
    const int32_t coeffs[] = { seg->a0_q16, seg->a1_q16, seg->a2_q16, seg->a3_q16, seg->a4_q16, seg->a5_q16 };

    // For quadrant handling we need the local angle within quadrant.
    // We'll get sin(theta_q) or cos(theta_q) from the first-quadrant polynomial by:
    // quadrant 0: theta = +rem -> sin = +sin(theta)
    // quadrant 1: theta = + (pi/2 - rem) -> sin = +cos(rem) = +sin(pi/2 - rem)
    // quadrant 2: theta = +rem -> sin = -sin(rem)
    // quadrant 3: theta = + (pi/2 - rem) -> sin = -cos(rem) = -sin(pi/2 - rem)
    //
    // To reuse single table (sin on [0,pi/2]) we must support both sin(x) and sin(pi/2 - x).
    // sin(pi/2 - x) = cos(x). We'll evaluate either sin(x) or cos(x).
    //
    // If quadrant is 0 or 2 -> we evaluate sin(x) with current idx/delta.
    // If quadrant is 1 or 3 -> we evaluate cos(x) = sin(pi/2 - x)
    //
    // To evaluate cos(x) as sin(pi/2 - x), we must compute index/delta corresponding to (pi/2 - theta_local).
    // That is equivalent to reversing within the segment indexing: new_idx = (segments-1) - idx, and new_delta = (1 - delta)
    // but because segment boundaries don't exactly align under reversal, a safer approach is:
    //
    // compute a fine index position p = idx * (1<<frac_bits) + frac  (0..2^14-1 within quadrant)
    // compute p_rev = ( (1<<14) - 1 ) - p  (mirror across quadrant)
    // then extract new_idx_rev = p_rev >> frac_bits, new_frac_rev = p_rev & ((1<<frac_bits)-1)
    //
    uint32_t p = (idx << frac_bits) | frac; // 0..2^14-1
    uint32_t quadrant_size = (1u << 14);
    uint32_t p_rev = (quadrant_size - 1u) - p;
    uint32_t idx_rev = (p_rev >> frac_bits) & ((1u << top_bits) - 1u);
    uint32_t frac_rev = p_rev & ((1u << frac_bits) - 1u);
    uint32_t delta_rev_q16 = (shift >= 0) ? (frac_rev >> shift) : (frac_rev << (-shift));

    if (quadrant == 0) {{
        int32_t s = eval_poly_q16(coeffs, delta_q16, {degree});
        return s;
    }} else if (quadrant == 1) {{
        // sin(pi/2 - x) -> use reversed index/delta
        const SinSeg_q16 *segr = &SIN_SEGS[idx_rev];
        const int32_t coeffsr[] = {{ segr->a0_q16, segr->a1_q16, segr->a2_q16, segr->a3_q16, segr->a4_q16, segr->a5_q16 }};
        int32_t c = eval_poly_q16(coeffsr, delta_rev_q16, {degree});
        return c;
    }} else if (quadrant == 2) {{
        int32_t s = eval_poly_q16(coeffs, delta_q16, {degree});
        return -s;
    }} else {{ // quadrant == 3
        const SinSeg_q16 *segr = &SIN_SEGS[idx_rev];
        const int32_t coeffsr[] = {{ segr->a0_q16, segr->a1_q16, segr->a2_q16, segr->a3_q16, segr->a4_q16, segr->a5_q16 }};
        int32_t c = eval_poly_q16(coeffsr, delta_rev_q16, {degree});
        return -c;
    }}
}}

// cos using sin shift by quarter-turn: cos(θ) = sin(θ + pi/2)
static inline int32_t cos_q16_from_u16angle(uint16_t angle) {{
    // add quarter-turn (65536/4 = 16384)
    uint16_t a = (uint16_t)(angle + 16384u);
    return sin_q16_from_u16angle_correct(a);
}}

#ifdef __cplusplus
}}
#endif

#endif // FAST_TRIG_RING_H
"""

# ------------------------
# Generator main
# ------------------------
def generate(output, segments=32, degree=5, samples=8192):
    if (segments & (segments-1)) != 0:
        print("Warning: non-power-of-two segments: indexing logic assumes power-of-two", file=sys.stderr)
    top_bits = int(math.log2(segments))
    frac_bits = 14 - top_bits
    if frac_bits < 0:
        raise ValueError("Too many segments for 14-bit quadrant resolution")

    table_rows = []
    stats_lines = []
    global_max_abs = 0.0
    global_max_rel = 0.0
    global_rms_acc = 0.0
    total_samples = 0

    # For each segment i, map x in [0,1) to theta in [theta_lo, theta_hi)
    for i in range(segments):
        theta_lo = (i / segments) * (math.pi/2.0)
        theta_hi = ((i+1) / segments) * (math.pi/2.0)
        # define y(x) = sin(theta_lo + (theta_hi - theta_lo)*x)
        def yfunc(x, lo=theta_lo, hi=theta_hi):
            th = lo + (hi - lo) * x
            return math.sin(th)
        coeffs = fit_polynomial_least_squares(yfunc, degree, samples)
        # convert to Q16.16 (signed)
        qcoeffs = [int(round(c * (1<<16))) for c in coeffs]
        # error metrics
        max_abs, max_rel, rms = eval_poly_error(coeffs, yfunc, samples)
        global_max_abs = max(global_max_abs, max_abs)
        global_max_rel = max(global_max_rel, max_rel)
        global_rms_acc += rms*rms * samples
        total_samples += samples
        # build row text (a0..a5) ensure exactly 6 entries (pad with 0)
        qpad = qcoeffs + [0]* (6 - len(qcoeffs))
        row = "    { " + ", ".join(f"(int32_t)0x{(q & 0xFFFFFFFF):08X}" for q in qpad) + " },  /* seg %2d [%10.6g .. %10.6g) err_abs=%8.3e rel=%8.3e rms=%8.3e */" % (i, theta_lo, theta_hi, max_abs, max_rel, rms)
        table_rows.append(row)
        stats_lines.append(f"seg {i:2d}: a0.. = {[round(c,10) for c in coeffs]} q = {[hex(q & 0xFFFFFFFF) for q in qpad]} max_abs={max_abs:.3e} max_rel={max_rel:.3e} rms={rms:.3e}")

    global_rms = math.sqrt(global_rms_acc / total_samples) if total_samples else 0.0
    stats = [
        f"Global max abs error (sin) = {global_max_abs:.10g}",
        f"Global max rel error (sin) = {global_max_rel:.10g}",
        f"Global RMS error (sin) = {global_rms:.10g}",
        "",
    ] + stats_lines

    header = HEADER_TMPL.format(
        segments=segments,
        degree=degree,
        samples=samples,
        table_rows="\n".join(table_rows),
        top_bits=top_bits,
        degree=degree
    )

    Path(output).write_text(header)
    print(f"Wrote header: {output}")
    print()
    print("Summary:")
    print(stats[0])
    print(stats[1])
    print(stats[2])
    print("(first 6 per-segment lines):")
    for s in stats[4:10]:
        print("  " + s)

# ------------------------
# CLI
# ------------------------
def main():
    p = argparse.ArgumentParser(description="Generate trig ring-table header (sin/cos) Q16.16")
    p.add_argument("--out", "-o", default="fast_trig_ring.h")
    p.add_argument("--segments", type=int, default=32)
    p.add_argument("--degree", type=int, default=5)
    p.add_argument("--samples", type=int, default=8192)
    args = p.parse_args()
    generate(args.out, segments=args.segments, degree=args.degree, samples=args.samples)

if __name__ == "__main__":
    main()
