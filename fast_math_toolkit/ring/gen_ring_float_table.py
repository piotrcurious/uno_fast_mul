#!/usr/bin/env python3
"""
generate_ring_float_ring_table.py

Generate a self-contained header implementing the "ring-table" fast
uint32_t × float multiplication for embedded targets (ESP32/Arduino).

This generator creates a per-segment linear chart for the mantissa m∈[1,2)
and emits c0,c1 in Q16.16 so the runtime evaluates:

    m_q16 ≈ c0_q16 + ((int64_t)c1_q16 * delta_q16 >> 16)

where delta_q16 ∈ [0, 2^16-1] is the local fractional position inside the segment
(obtained from float mantissa bits). Final integer multiplication uses a single
32×32→64 hardware multiply and shift.

Defaults:
  segments = 16
  samples_per_segment = 4096 (for fitting & error analysis)
  output header = fast_ring_tables.h

The header contains:
  - typedef RingMantSeg { uint32_t c0_q16; int32_t c1_q16; }
  - const RingMantSeg RING_MANT_TABLE[16] = { ... };
  - mul_u32_by_float_ring(uint32_t a, float f) implementation

Author: ChatGPT (generator)
"""
from pathlib import Path
import argparse
import math
import struct
import sys

# ============================
# Utilities
# ============================
def float_to_bits(f):
    return struct.unpack("<I", struct.pack("<f", f))[0]

def bits_to_float(u):
    return struct.unpack("<f", struct.pack("<I", u))[0]

def q16_of_float(f):
    """Round float->Q16.16 unsigned (clamped to uint32 range)."""
    if not math.isfinite(f) or f <= 0.0:
        return 0
    v = f * (1 << 16)
    if v >= 0xFFFFFFFF:
        return 0xFFFFFFFF
    return int(round(v))

# ============================
# Fitting per-segment
# ============================
def fit_segment_linear(a, b, samples=4096):
    """
    Fit y = A + B * x on x in [0,1) where y = m (mantissa in [a,b]).
    Use uniform sampling of x and least-squares fit.
    Returns (A, B) as floats.
    """
    # sample x_j uniformly in [0,1), compute y_j = a + (b-a)*x
    n = samples
    Sx = Sxx = Sy = Sxy = 0.0
    for j in range(n):
        x = (j + 0.5) / n
        y = a + (b - a) * x
        Sx += x
        Sxx += x * x
        Sy += y
        Sxy += x * y
    denom = n * Sxx - Sx * Sx
    if abs(denom) < 1e-30:
        B = 0.0
    else:
        B = (n * Sxy - Sx * Sy) / denom
    A = (Sy - B * Sx) / n
    return A, B

def evaluate_segment_error(A, B, a, b, samples=4096):
    """
    Evaluate approximation error on segment m in [a,b]:
      y_true = m, x in [0,1)
      y_approx = A + B*x
    Return (max_abs_err, max_rel_err, rms_err)
    """
    n = samples
    max_abs = 0.0
    max_rel = 0.0
    sse = 0.0
    for j in range(n):
        x = (j + 0.5) / n
        y = a + (b - a) * x
        yhat = A + B * x
        err = yhat - y
        abs_err = abs(err)
        rel_err = abs_err / abs(y) if y != 0 else abs_err
        if abs_err > max_abs: max_abs = abs_err
        if rel_err > max_rel: max_rel = rel_err
        sse += err * err
    rms = math.sqrt(sse / n)
    return max_abs, max_rel, rms

# ============================
# Header generation
# ============================
HEADER_PROLOG = r"""/*
  fast_ring_tables.h
  -------------------
  Auto-generated ring-table header (Q16.16) for fast uint32 * float on embedded MCUs.

  Generation parameters:
    segments = {segments}
    samples_per_segment = {samples}

  Table format:
    typedef struct {{
      uint32_t c0_q16;   // Q16.16 representation of A
      int32_t  c1_q16;   // Q16.16 representation of B
    }} RingMantSeg;

  Runtime evaluation:
    - decompose float bits into exponent and 23-bit mantissa with implicit 1
    - segment index = top 4 bits of mantissa (for 16 segments)
    - delta_q16 = lower bits of mantissa mapped to [0..2^16-1]
    - m_q16 = c0_q16 + ((int64_t)c1_q16 * delta_q16 >> 16)
    - result = (a * m_q16) >> 16, then shift by exponent

  Notes:
    - Unsigned semantics: negative floats saturate to 0.
    - Uses 64-bit intermediate multiply (efficient on ESP32).
*/

#ifndef FAST_RING_TABLES_H
#define FAST_RING_TABLES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <math.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RING_MANT_SEGMENTS
#define RING_MANT_SEGMENTS {segments}
#endif

typedef struct {{
    uint32_t c0_q16;
    int32_t  c1_q16;
}} RingMantSeg;

static const RingMantSeg RING_MANT_TABLE[RING_MANT_SEGMENTS] = {{
{table_entries}
}};

/* Generator statistics (per-segment):
{stats_comment}
*/

static inline uint32_t mul_u32_by_q16(uint32_t a, uint32_t mul_q16) {{
    uint64_t prod = (uint64_t)a * (uint64_t)mul_q16;
    return (uint32_t)(prod >> 16);
}}

/* Core: multiply uint32 by variable float using ring-table */
static inline uint32_t mul_u32_by_float_ring(uint32_t a, float f) {{
    if (a == 0u) return 0u;
    if (!isfinite(f) || f == 0.0f) return 0u;
    if (f < 0.0f) return 0u; // unsigned saturating

    union {{ float f; uint32_t u; }} xu;
    xu.f = f;
    int exp = (int)((xu.u >> 23) & 0xFF) - 127;
    uint32_t mant = (xu.u & 0x7FFFFFu) | 0x800000u; // 1.mant (Q1.23)

    // 16 segments -> use top 4 bits of mantissa to index
    const int top_bits = 4;
    const int idx = (int)((mant >> (23 - top_bits)) & ((1u << top_bits) - 1));

    // remaining fractional bits:
    const int frac_bits = 23 - top_bits; // 19
    const uint32_t frac_mask = ((1u << frac_bits) - 1u);
    uint32_t frac = mant & frac_mask; // 0 .. (2^frac_bits - 1)

    // Map frac -> delta_q16 (0 .. 2^16-1)
    // delta_q16 = round( frac * (2^16) / 2^frac_bits )
    // We'll compute by shifting where possible to be fast:
    // shift = frac_bits - 16 (here 3). So delta_q16 = frac >> shift
    const int shift = frac_bits - 16;
    uint32_t delta_q16 = (shift >= 0) ? (frac >> shift) : (frac << (-shift));

    // fetch segment coefficients
    RingMantSeg seg = RING_MANT_TABLE[idx];

    // m_q16 = c0 + (c1 * delta_q16 >> 16)
    int64_t corr = (int64_t)seg.c1_q16 * (int64_t)delta_q16;
    uint32_t m_q16 = seg.c0_q16 + (uint32_t)(corr >> 16);

    // multiply and apply exponent
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

#endif // FAST_RING_TABLES_H
"""

# ============================
# Main generator logic
# ============================
def generate_header(path, segments=16, samples_per_segment=4096):
    S = segments
    stats = []
    table_entries = []
    global_max_abs = 0.0
    global_max_rel = 0.0
    global_rms_sum = 0.0
    total_samples = 0

    for i in range(S):
        a = 1.0 + float(i) / S
        b = 1.0 + float(i + 1) / S
        # fit linear A + B*x where x in [0,1)
        A, B = fit_segment_linear(a, b, samples=samples_per_segment)
        # Convert to Q16
        c0 = int(round(A * (1 << 16)))
        c1 = int(round(B * (1 << 16)))
        # Evaluate errors with high-resolution sampling (we'll use samples_per_segment)
        max_abs, max_rel, rms = evaluate_segment_error(A, B, a, b, samples=samples_per_segment)
        stats.append((i, a, b, A, B, c0, c1, max_abs, max_rel, rms))
        if max_abs > global_max_abs: global_max_abs = max_abs
        if max_rel > global_max_rel: global_max_rel = max_rel
        global_rms_sum += rms * rms * samples_per_segment
        total_samples += samples_per_segment

        # format c0,c1 as hex
        c0_hex = f"0x{c0 & 0xFFFFFFFF:08X}u"
        # c1 is signed 32-bit value
        c1_s = c1 & 0xFFFFFFFF
        c1_hex = f"0x{c1_s:08X}"
        table_entries.append(f"    {{ {c0_hex}, (int32_t){c1_hex} }},  /* seg {i}: m in [{a:.6f},{b:.6f}) */")

    global_rms = math.sqrt(global_rms_sum / total_samples) if total_samples else 0.0

    stats_lines = []
    stats_lines.append(f" Global max abs error (mantissa) = {global_max_abs:.10g}")
    stats_lines.append(f" Global max rel error (mantissa) = {global_max_rel:.10g}")
    stats_lines.append(f" Global RMS error (mantissa) = {global_rms:.10g}")
    stats_lines.append("")
    stats_lines.append(" Per-segment detail (index, a, b, A, B, c0_q16, c1_q16, max_abs, max_rel, rms):")
    for st in stats:
        i,a,b,A,B,c0,c1,max_abs,max_rel,rms = st
        stats_lines.append(f"  seg {i:2d}: [{a:.6f},{b:.6f}) A={A:.10g} B={B:.10g} c0=0x{c0:08X} c1=0x{(c1 & 0xFFFFFFFF):08X} max_abs={max_abs:.10g} max_rel={max_rel:.10g} rms={rms:.10g}")

    header_text = HEADER_PROLOG.format(
        segments=S,
        samples=samples_per_segment,
        table_entries="\n".join(table_entries),
        stats_comment="\n".join("    " + line for line in stats_lines)
    )

    Path(path).write_text(header_text)
    print(f"Wrote header: {path}")
    print()
    print("Summary:")
    print(stats_lines[0])
    print(stats_lines[1])
    print(stats_lines[2])
    print()
    print("Per-segment errors (first 4 shown):")
    for st in stats[:4]:
        i,a,b,A,B,c0,c1,max_abs,max_rel,rms = st
        print(f" seg {i}: max_abs={max_abs:.6g}, max_rel={max_rel:.6g}, rms={rms:.6g}")

def main():
    p = argparse.ArgumentParser(description="Generate ring-table header for fast uint32*float (16 segments default).")
    p.add_argument("--out", "-o", default="fast_ring_tables.h", help="Output header file")
    p.add_argument("--segments", type=int, default=16, help="Number of mantissa segments (power-of-two recommended)")
    p.add_argument("--samples", type=int, default=4096, help="Samples per segment used for fitting and error analysis")
    args = p.parse_args()

    if args.segments & (args.segments - 1) != 0:
        print("Warning: non-power-of-two segments may require different bit extraction logic.", file=sys.stderr)
    generate_header(args.out, segments=args.segments, samples_per_segment=args.samples)

if __name__ == "__main__":
    main()
