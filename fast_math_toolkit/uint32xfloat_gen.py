#!/usr/bin/env python3
"""
generate_ring_tables.py

Generate a self-contained Arduino/ESP32 header implementing:
 - per-segment polynomial coefficient tables for log2(1+t) (formal-series/Taylor grounded)
 - Q-format coefficient scaling and runtime Horner evaluation
 - a 'RingTable' of float constants stored as Q16.16 multipliers to optimize uint32*float
 - helper runtime functions: fast multiply-by-constant, fallback conversion, and fast approx log2

Usage example:
    python3 generate_ring_tables.py --out fast_ring_tables.h \
        --segments 16 --degree 3 --coeff-q 24 \
        --floats 0.5 1.5 2.0 0.70710678

The produced header is standalone (no external deps) and tuned for ESP32 (uses uint64_t ops).
"""

from pathlib import Path
import math
import argparse
import textwrap

# ---------- utility math (Taylor-based local expansion) ----------

LN2 = math.log(2.0)

def taylor_log2_coeffs_at(tc, degree):
    """
    Return coefficients [a0, a1, ..., a_degree] for the Taylor expansion of
    f(t) = log2(1 + t) around t = tc:
        f(t) = sum_{k=0..degree} a_k * (t - tc)^k   + O((t-tc)^{degree+1})
    The coefficients are exact floats (we scale them later).
    """
    coeffs = []
    # a0 = f(tc)
    a0 = math.log(1.0 + tc) / LN2
    coeffs.append(a0)
    # for k>=1: derivative formula gives
    # f^{(k)}(t) = (d^k/dt^k) log(1+t) / ln 2
    # d^k/dt^k log(1+t) = (-1)^{k-1} (k-1)! / (1+t)^k
    # So a_k = f^{(k)}(tc) / k! = (-1)^{k-1} / (ln2 * k * (1+tc)^k)
    for k in range(1, degree+1):
        ak = ((-1.0)**(k-1)) / (LN2 * k * ((1.0 + tc)**k))
        coeffs.append(ak)
    return coeffs

# ---------- generator -----------------------------------------------------

HEADER_TEMPLATE = """\
/*
    fast_ring_tables.h
    -------------------
    Auto-generated header (ESP32 / Arduino compatible).

    Features:
      - Per-segment Taylor polynomials approximating log2(1+t) on mantissa
        charts (t in [0,1)). These are 'formal' / truncated power-series
        representatives (coefficients scaled to integer Q format).
      - A RingTable of float constants pre-converted to Q16.16 multipliers
        for ultra-fast uint32_t * constant multiplication via a single
        32x32->64 multiply and a right shift.
      - Helper routines: multiply-by-constant (fast), multiply-by-float
        (fallback), fast approximate log2 for uint32 inputs using the
        per-segment polynomials.

    Generator options embedded here:
      segments = {segments}
      degree   = {degree}
      coeff_q  = {coeff_q}
      mantissa_bits = {mantissa_bits}

    Notes:
      - The log2 polynomials are stored as coefficients for the polynomial
        in variable u = t - t_center where t = mantissa/2^{mantissa_bits} - 1.
      - Coefficients are scaled by COEFF_SCALE = 1 << {coeff_q} (signed ints).
      - At runtime we compute u in the matching Q domain:
            u_q = (mantissa - {mantissa_base}) << {u_shift}
        where u_shift = COEFF_Q - mantissa_bits (see implementation).
      - The Q16 multipliers for ring floats are simply round(float * 2^16).
*/

#ifndef FAST_RING_TABLES_H
#define FAST_RING_TABLES_H

#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#ifdef ARDUINO
#include <avr/pgmspace.h>
#endif

// ------------------------------------------------------------------
// Config (generated)
// ------------------------------------------------------------------
#define FRM_SEGMENTS    ({segments})
#define FRM_DEGREE      ({degree})
#define FRM_COEFF_Q     ({coeff_q})
#define FRM_COEFF_SCALE (1u << FRM_COEFF_Q)
#define FRM_MANT_BITS   ({mantissa_bits})
#define FRM_MANT_BASE   ({mantissa_base})
#define FRM_U_SHIFT     ({u_shift})  // (FRM_COEFF_Q - FRM_MANT_BITS)

#ifdef __cplusplus
extern "C" {{
#endif

// ------------------------------------------------------------------
// Tables: polynomial coefficients for log2(1+t) per-segment
// storage: int32_t log2_poly_coeffs[SEGMENTS][DEGREE+1] (scale = FRM_COEFF_SCALE)
// coefficient order: a0, a1, a2, ... (Horner is applied with (u = t - center))
// ------------------------------------------------------------------

static const int32_t LOG2_POLY_COEFFS[FRM_SEGMENTS][FRM_DEGREE+1] = {{
{coeff_array}
}};

// centers (t center for each segment) as rational (numerator/denom) used at runtime
static const uint32_t LOG2_SEG_CENTER_NUM[FRM_SEGMENTS] = {{ {centers_num} }};
static const uint32_t LOG2_SEG_CENTER_DEN = {centers_den};

// ------------------------------------------------------------------
// Ring table for float constants -> Q16 multipliers
// ------------------------------------------------------------------
typedef struct {{
    float f;         // original float (for matching / debugging)
    uint32_t q16;    // multiplier = round(f * 2^16)
}} RingFloatEntry;

static const RingFloatEntry RING_FLOAT_TABLE[] = {{
{float_entries}
}};
static const size_t RING_FLOAT_TABLE_SIZE = sizeof(RING_FLOAT_TABLE)/sizeof(RING_FLOAT_TABLE[0]);

// ------------------------------------------------------------------
// Runtime helpers
// ------------------------------------------------------------------

// Convert float to Q16.16 multiplier (round)
static inline uint32_t float_to_q16_multiplier(float f) {{
    // handle special cases conservatively
    if (!isfinite(f)) return 0;
    // clamp to UINT32 range (approx)
    double v = (double)f * (1 << 16);
    if (v >= (double)0xFFFFFFFFu) return 0xFFFFFFFFu;
    if (v <= 0.0) return 0u;
    return (uint32_t)(v + 0.5);
}}

// Fast multiply of uint32 by a Q16 multiplier
static inline uint32_t mul_u32_by_q16(uint32_t a, uint32_t mul_q16) {{
    // Computes (a * mul_q16) >> 16 using 64-bit intermediate
    uint64_t prod = (uint64_t)a * (uint64_t)mul_q16;
    return (uint32_t)(prod >> 16);
}}

// Fast multiply using table index (very fast)
static inline uint32_t mul_u32_by_const_index(uint32_t a, size_t idx) {{
    if (idx >= RING_FLOAT_TABLE_SIZE) return 0;
    return mul_u32_by_q16(a, RING_FLOAT_TABLE[idx].q16);
}}

// Fallback multiply-by-float: tries to find float in table, else converts and computes
static inline uint32_t mul_u32_by_float(uint32_t a, float f) {{
    // first try exact match in the table
    for (size_t i = 0; i < RING_FLOAT_TABLE_SIZE; ++i) {{
        if (RING_FLOAT_TABLE[i].f == f) return mul_u32_by_q16(a, RING_FLOAT_TABLE[i].q16);
    }}
    // not found: convert and multiply
    uint32_t m = float_to_q16_multiplier(f);
    return mul_u32_by_q16(a, m);
}}

// ------------------------------------------------------------------
// Fast approximate log2 for a positive uint32 value using formal-local polynomials
// Output: returns fixed-point Q8.8 approximation of log2(value)
// If value == 0 returns INT32_MIN sentinel
// ------------------------------------------------------------------
static inline int32_t fast_log2_q8_u32(uint32_t v) {{
    if (v == 0) return INT32_MIN;
    // find msb (floor(log2(v)))
    int e = -1;
    uint32_t tv = v;
    if (tv >= 0x10000u) {{
        tv >>= 16; e += 16;
    }}
    if (tv >= 0x100u) {{
        tv >>= 8; e += 8;
    }}
    // now tv < 256, use byte msb table emulation
    // simple loop to find msb (cheap for small constants)
    for (int b = 7; b >= 0; --b) {{
        if (tv & (1u << b)) {{ e += (b+1); break; }}
    }}
    // e is floor(log2(v))
    // compute mantissa m (FRM_MANT_BITS bits) by shifting v to have top bit at position (FRM_MANT_BITS-1)
    int shift = e - (FRM_MANT_BITS - 1);
    uint32_t mant;
    if (shift >= 0) mant = (v >> shift) & ((1u << FRM_MANT_BITS) - 1);
    else mant = (v << (-shift)) & ((1u << FRM_MANT_BITS) - 1);

    // compute t = mant / 2^{FRM_MANT_BITS} - 1  => in rational form: (mant - base)/base
    int32_t delta = (int32_t)mant - (int32_t)FRM_MANT_BASE; // signed small
    // u_q = delta * (1 << FRM_U_SHIFT)  (Q = FRM_COEFF_Q)
    int32_t u_q = delta << FRM_U_SHIFT; // fits in 32-bit for FRM choices

    // choose segment index by mapping t in [0,1) to 0..SEGMENTS-1
    // t approx = delta / base, so idx = floor( (delta / base) * segments )
    int idx = (delta * FRM_SEGMENTS) / FRM_MANT_BASE;
    if (idx < 0) idx = 0;
    if (idx >= FRM_SEGMENTS) idx = FRM_SEGMENTS - 1;

    // evaluate polynomial via Horner in signed 64-bit: coefficients are Q=FRM_COEFF_Q
    int64_t acc = LOG2_POLY_COEFFS[idx][FRM_DEGREE]; // highest coeff
    for (int k = FRM_DEGREE - 1; k >= 0; --k) {{
        // acc = coeff_k + acc * u_q / COEFF_SCALE
        int64_t prod = (acc * (int64_t)u_q); // Q = FRM_COEFF_Q * 2
        acc = LOG2_POLY_COEFFS[idx][k] + (prod >> FRM_COEFF_Q);
    }}
    // acc is log2(mantissa) scaled by FRM_COEFF_SCALE (i.e. Q = FRM_COEFF_Q)
    // convert to Q8.8: first produce integer part (e - (FRM_MANT_BITS-1)) and add fractional
    int32_t integer_part = e - (FRM_MANT_BITS - 1);
    // fractional part in Q8.8 = (acc * 2^8) / 2^{FRM_COEFF_Q}
    int32_t frac_q8 = (int32_t)(acc >> (FRM_COEFF_Q - 8));
    int32_t result_q8 = (integer_part << 8) + frac_q8;
    return result_q8;
}}

// ------------------------------------------------------------------
// End
// ------------------------------------------------------------------

#ifdef __cplusplus
}}
#endif

#endif // FAST_RING_TABLES_H
"""

def generate_header(outpath, segments, degree, coeff_q, mantissa_bits, float_constants):
    # Parameters
    mant_base = 1 << (mantissa_bits - 1)  # typically 128 for mantissa_bits=8
    # compute u_shift = coeff_q - mantissa_bits + ??? we want u_q = (mant - base) * (1<<u_shift)
    # t = (mant / 2^mantissa_bits) - 1  => t = (mant - base) / base
    # choose u_q = t * (1<<coeff_q) = (mant - base) * (1<<coeff_q) / base
    # to implement as left shift we set u_q = (mant - base) << u_shift where u_shift = coeff_q - mantissa_bits + ???:
    # Actually (1<<coeff_q)/base = (1<<coeff_q)/(1<<(mantissa_bits-1)) = 1 << (coeff_q - (mantissa_bits-1))
    # So u_shift = coeff_q - (mantissa_bits - 1)
    u_shift = coeff_q - (mantissa_bits - 1)

    # Build coefficient arrays
    centers = []
    coeff_arrays = []
    for i in range(segments):
        # t range [0,1): map i -> center = (i + 0.5)/segments
        tc = (i + 0.5) / float(segments)  # in (0,1)
        centers.append(tc)
        coeffs = taylor_log2_coeffs_at(tc, degree)
        # scale each coefficient by 2^coeff_q and round to int32
        scaled = [int(round(c * (1 << coeff_q))) for c in coeffs]
        # ensure fits in int32 (should for reasonable params)
        coeff_arrays.append(scaled)

    # Build header pieces
    # coeff_array textual
    coeff_lines = []
    for row in coeff_arrays:
        coeff_lines.append("    { " + ", ".join(str(int(x)) for x in row) + " },")
    coeff_array_text = "\n".join(coeff_lines)

    # centers numerators/denominators
    # We'll store centers as rational numerators with a common denominator = segments * 2
    centers_den = segments * 2
    centers_num_list = [str(int(round(c * centers_den))) for c in centers]
    centers_num_text = ", ".join(centers_num_list)

    # float entries
    float_entries_lines = []
    for f in float_constants:
        q16 = int(round(float(f) * (1 << 16)))
        float_entries_lines.append("    { % .9g, %du }," % (float(f), q16))
    float_entries_text = "\n".join(float_entries_lines)

    header_text = HEADER_TEMPLATE.format(
        segments=segments,
        degree=degree,
        coeff_q=coeff_q,
        mantissa_bits=mantissa_bits,
        mantissa_base=mant_base,
        u_shift=u_shift,
        coeff_array=coeff_array_text,
        centers_num=centers_num_text,
        centers_den=centers_den,
        float_entries=float_entries_text
    )

    Path(outpath).write_text(header_text)
    print(f"Generated header: {outpath}")

# ---------- CLI ----------

def cli():
    p = argparse.ArgumentParser(description="Generate ring-table-based fast header for ESP32 / Arduino")
    p.add_argument("--out", "-o", default="fast_ring_tables.h")
    p.add_argument("--segments", type=int, default=16)
    p.add_argument("--degree", type=int, default=3)
    p.add_argument("--coeff-q", type=int, default=24, help="Q scaling used to store polynomial coeffs")
    p.add_argument("--mantissa-bits", type=int, default=8, help="bits of mantissa used in normalization (typical 8)")
    p.add_argument("--floats", nargs="*", default=["0.5","1.0","1.5","2.0","0.70710678"], help="float constants to precompute as ring multipliers")
    args = p.parse_args()
    generate_header(args.out, args.segments, args.degree, args.coeff_q, args.mantissa_bits, args.floats)

if __name__ == "__main__":
    cli()
