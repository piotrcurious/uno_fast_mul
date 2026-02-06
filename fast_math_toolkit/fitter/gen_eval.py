#!/usr/bin/env python3
"""
generate_chebyshev_remez.py

Generate a self-contained C header implementing a Chebyshev (minimax) polynomial
approximation computed via a discrete Remez exchange algorithm with dense
(Lebesgue-style) sampling. The header evaluates the polynomial in Q16.16 fixed-point
using Clenshaw recurrence for Chebyshev basis.

Inputs:
  - CSV file of pairs (x_uint32,y_float) where x_uint32 spans 0..2^32-1 (angle normalization)
  - OR one of built-in targets: sin, cos, tanh, etc. (default: sin over full uint32 range)

Outputs:
  - C header with Chebyshev coefficients in Q16.16 and function:
      int32_t cheb_eval_q16(uint32_t x); // returns Q16.16

Author: ChatGPT
"""

from pathlib import Path
import math
import argparse
import struct
import sys
from typing import Tuple, List

# Try to import numpy for numeric robustness; fall back if missing.
try:
    import numpy as np
    NP = True
except Exception:
    NP = False

# ---------------------------
# Utility functions
# ---------------------------
UINT32_MAX_VAL = 0xFFFFFFFF
UINT32_DEN = float(UINT32_MAX_VAL)  # map x in [0..2^32-1] to [0,1] as x/UINT32_MAX

def load_csv_samples(csv_path: Path) -> Tuple[List[int], List[float]]:
    xs = []
    ys = []
    with csv_path.open("r") as f:
        for ln in f:
            ln = ln.strip()
            if not ln or ln.startswith("#"):
                continue
            parts = ln.split(",")
            if len(parts) < 2:
                continue
            x = int(parts[0].strip())
            y = float(parts[1].strip())
            xs.append(x)
            ys.append(y)
    return xs, ys

def default_target_samples(target_name: str, n_samples: int):
    """
    Produce sample arrays x_uint32 (0..2^32-1) and y = target(theta).
    The x is interpreted as angle across full uint32 range mapped to [0,2*pi).
    """
    xs = []
    ys = []
    for i in range(n_samples):
        u = float(i) / float(n_samples - 1)  # in [0,1]
        x = int(round(u * UINT32_MAX_VAL))
        theta = u * 2.0 * math.pi
        if target_name.lower() == "sin":
            y = math.sin(theta)
        elif target_name.lower() == "cos":
            y = math.cos(theta)
        elif target_name.lower() == "tanh":
            y = math.tanh(theta)
        else:
            # default sin
            y = math.sin(theta)
        xs.append(x)
        ys.append(y)
    return xs, ys

def x_to_t_float(x_uint32: int):
    """Map uint32 x to Chebyshev domain t in [-1,1], using float math (for use in generator)."""
    s = float(x_uint32) / UINT32_DEN  # [0,1]
    t = 2.0 * s - 1.0
    return t

def t_to_q16(t: float) -> int:
    """Convert t in [-1,1] to signed Q16.16 int32."""
    # clamp
    if t > 1.0: t = 1.0
    if t < -1.0: t = -1.0
    return int(round(t * (1 << 16)))

# ---------------------------
# Chebyshev basis helpers
# ---------------------------
def chebyshev_matrix(t_values: List[float], degree: int):
    """Return matrix T of shape (len(t_values), degree+1) where T[:,k] = T_k(t)."""
    n = len(t_values)
    d = degree
    # Build via recurrence T_0=1, T_1=t, T_k=2*t*T_{k-1}-T_{k-2}
    import math
    T = [[0.0]*(d+1) for _ in range(n)]
    for i,t in enumerate(t_values):
        if d >= 0:
            T[i][0] = 1.0
        if d >= 1:
            T[i][1] = t
        for k in range(2, d+1):
            T[i][k] = 2.0 * t * T[i][k-1] - T[i][k-2]
    return T

def chebyshev_eval_clenshaw_float(coeffs: List[float], t: float):
    """Evaluate Chebyshev series given coeffs (a0..an) at point t (float) using Clenshaw."""
    n = len(coeffs) - 1
    b_kplus1 = 0.0
    b_kplus2 = 0.0
    for k in reversed(range(n+1)):
        b_k = 2.0 * t * b_kplus1 - b_kplus2 + coeffs[k]
        b_kplus2 = b_kplus1
        b_kplus1 = b_k
    return b_kplus1 - t * b_kplus2

# ---------------------------
# Linear algebra helpers (fallback if numpy is missing)
# ---------------------------
def solve_linear_system_py(A, b):
    """
    Solve Ax=b by Gaussian elimination. A is list of lists, b is list.
    Returns solution vector x.
    """
    n = len(b)
    # build augmented
    M = [row[:] + [b_i] for row, b_i in zip(A, b)]
    for k in range(n):
        # pivot
        piv = k
        maxv = abs(M[k][k])
        for i in range(k+1, n):
            if abs(M[i][k]) > maxv:
                maxv = abs(M[i][k]); piv = i
        if maxv < 1e-18:
            raise RuntimeError("Singular matrix in solver")
        if piv != k:
            M[k], M[piv] = M[piv], M[k]
        # normalize
        div = M[k][k]
        for j in range(k, n+1):
            M[k][j] /= div
        # eliminate
        for i in range(k+1, n):
            fac = M[i][k]
            if fac == 0.0: continue
            for j in range(k, n+1):
                M[i][j] -= fac * M[k][j]
    # back substitution
    x = [0.0]*n
    for i in range(n-1, -1, -1):
        x[i] = M[i][n]
        for j in range(i+1, n):
            x[i] -= M[i][j]*x[j]
    return x

# ---------------------------
# Remez exchange (discrete) algorithm
# ---------------------------
def initial_extremal_points_chebyshev(num_extreme: int) -> List[float]:
    """
    Return initial nodes in t ∈ [-1,1] using Chebyshev extremal points:
    t_k = cos(pi * (2k+1) / (2N)), k=0..N-1 for N=num_extreme.
    """
    N = num_extreme
    pts = []
    for k in range(N):
        tk = math.cos(math.pi * (2*k + 1) / (2.0 * N))
        pts.append(tk)
    # these go from near 1 down to -1, reverse to increasing order
    pts = list(reversed(pts))
    return pts

def find_local_extrema_indices(errs: List[float]) -> List[int]:
    """Return indices i where |err[i]| is a local maxima w.r.t neighbors (including endpoints)."""
    n = len(errs)
    idxs = []
    if n == 0:
        return idxs
    # endpoints
    if abs(errs[0]) >= abs(errs[1]):
        idxs.append(0)
    for i in range(1, n-1):
        if (abs(errs[i]) >= abs(errs[i-1])) and (abs(errs[i]) >= abs(errs[i+1])):
            # also require it's a sign change or local extremum
            idxs.append(i)
    if abs(errs[-1]) >= abs(errs[-2]):
        idxs.append(n-1)
    return idxs

def select_alternating_points(candidate_idxs: List[int], errs: List[float], want: int) -> List[int]:
    """
    From candidate extrema indices choose 'want' indices that alternate in sign and roughly spaced.
    Strategy:
      - sort candidates by absolute error descending
      - attempt to build alternating sequence by greedily picking largest error points while keeping alternating sign and ordering by x
      - if we cannot get 'want', expand by taking next candidates until satisfied
    This is heuristic but works reasonably for discrete Remez.
    """
    # sort candidates by abs error desc
    cand = sorted(candidate_idxs, key=lambda i: -abs(errs[i]))
    chosen = []
    # try all possible starting candidates among top few to find alternating sequence
    max_try = min(10, len(cand))
    for start_idx in range(max_try):
        chosen = [cand[start_idx]]
        last_sign = 1 if errs[cand[start_idx]] >= 0 else -1
        # build forward by selecting next candidate with opposite sign and not too close
        for c in cand:
            if c == chosen[-1]: continue
            s = 1 if errs[c] >= 0 else -1
            if s == last_sign: continue
            # ensure ordering not violate monotonic x ordering when inserted
            chosen.append(c)
            last_sign = s
            if len(chosen) >= want:
                break
        if len(chosen) >= want:
            break
    if len(chosen) < want:
        # fallback: take top 'want' indices and sort by x
        chosen = sorted(cand[:want])
        # try to enforce alternation by flipping some signs if necessary
        # simple pass to pick alternating by scanning sorted order and picking
        out = []
        last_sign = 0
        for idx in chosen:
            s = 1 if errs[idx] >= 0 else -1
            if not out:
                out.append(idx); last_sign = s
            elif s != last_sign:
                out.append(idx); last_sign = s
            if len(out) == want: break
        if len(out) < want:
            # fill remaining by closest candidates regardless of sign
            remain = [c for c in cand if c not in out]
            for r in remain:
                out.append(r)
                if len(out) == want: break
        chosen = out
    # finally sort chosen by index to be monotonic in x
    chosen = sorted(chosen)
    return chosen

def remez_discrete(xs_t: List[float], ys: List[float], degree: int,
                   grid_t: List[float], max_iter=30, tol=1e-12, verbose=False):
    """
    Discrete Remez exchange algorithm on sample points (grid_t) where xs_t, ys are functions defined on domain.
    xs_t: mapping sample points -> t in [-1,1] (but we only need grid_t)
    ys: function samples for the grid_t values (same length)
    Note: xs_t and grid_t may be identical; we expect grid_t to be fine-resolution.
    Returns Chebyshev coefficients (float) of degree 'degree'.
    """
    N = degree
    m = N + 2  # number of alternation points
    # initial extremal points using Chebyshev nodes mapped onto grid: find nearest grid indices
    cheb_nodes = initial_extremal_points_chebyshev(m)
    # map nodes to nearest indices in grid_t
    def nearest_idx(t):
        # binary search since grid_t is sorted ascending
        import bisect
        i = bisect.bisect_left(grid_t, t)
        if i == 0: return 0
        if i >= len(grid_t): return len(grid_t)-1
        # pick closer
        if abs(grid_t[i] - t) < abs(grid_t[i-1] - t):
            return i
        else:
            return i-1
    alt_idx = [nearest_idx(t) for t in cheb_nodes]
    # ensure alt_idx sorted
    alt_idx = sorted(set(alt_idx))
    # ensure we have m points; if duplicates, expand by nearest unused indices
    if len(alt_idx) < m:
        # pad by evenly spaced indices
        L = len(grid_t)
        step = max(1, L // (m+1))
        alt_idx = list(range(0, L, step))[:m]

    last_E = None
    for iteration in range(max_iter):
        # build linear system to solve for Chebyshev coefficients c0..cN and E
        # For alternation indices k=0..m-1:
        # sum_{j=0..N} c_j T_j(t_k) + s_k * E = y_k   where s_k = (-1)^k
        m_pts = len(alt_idx)
        A = [[0.0]*(N+1 + 1) for _ in range(m_pts)]  # last col for E
        b = [0.0]*m_pts
        for ii, idx in enumerate(alt_idx):
            tk = grid_t[idx]
            yk = ys[idx]
            # compute T_j(tk)
            Trow = [0.0]*(N+1)
            Trow[0] = 1.0
            if N >= 1:
                Trow[1] = tk
            for j in range(2, N+1):
                Trow[j] = 2.0 * tk * Trow[j-1] - Trow[j-2]
            for j in range(N+1):
                A[ii][j] = Trow[j]
            A[ii][N+1-1] = (1.0 if (ii % 2 == 0) else -1.0)  # correction: last column is E, but index N+1-1 used incorrectly; fix below
            # We'll set last column properly:
            b[ii] = yk
        # fix A last column: index N+1 is out of bound earlier; rebuild last column properly
        for ii in range(m_pts):
            A[ii] = A[ii][:N+1] + [ (1.0 if (ii % 2 == 0) else -1.0) ]
        # Solve linear system A_full * x = b, where x = [c0..cN, E]
        # A_full is m_pts x (N+2). But m_pts should equal N+2 ideally.
        if len(A) != (N+2):
            # if not enough, pad or trim
            # choose first N+2 points from alt_idx
            if len(A) < (N+2):
                # pick extra indices uniformly
                L = len(grid_t)
                step = max(1, L // (N+2))
                alt_idx = list(range(0, L, step))[:(N+2)]
                # rebuild system for new alt_idx
                continue
            else:
                # trim to first N+2
                A = A[:(N+2)]
                b = b[:(N+2)]

        # Convert to numpy arrays if available
        x_solution = None
        if NP:
            import numpy as _np
            Af = _np.array(A, dtype=_np.float64)
            bf = _np.array(b, dtype=_np.float64)
            try:
                sol = _np.linalg.solve(Af, bf)
                x_solution = [float(x) for x in sol]
            except Exception as e:
                # fall back to pseudo-inverse least-squares
                sol, *_ = _np.linalg.lstsq(Af, bf, rcond=None)
                x_solution = [float(x) for x in sol]
        else:
            # use pure python solver; A is square (N+2) x (N+2)
            try:
                x_solution = solve_linear_system_py(A, b)
            except Exception as e:
                raise RuntimeError("Linear solver failed: " + str(e))

        # Extract c_j and E
        c = x_solution[:N+1]
        E = x_solution[N+1] if len(x_solution) > N+1 else 0.0

        # compute error on dense grid (Lebesgue sampling)
        # err_i = p(t_i) - y_i
        # build T matrix for grid evaluation more cheaply: use Clenshaw eval with c as Chebyshev coefficients
        errs = []
        for t_i, y_i in zip(grid_t, ys):
            p_t = chebyshev_eval_clenshaw_float(c, t_i)
            errs.append(p_t - y_i)

        # find local extrema indices
        candidate_idxs = find_local_extrema_indices(errs)
        # ensure we have enough candidate extremes; if not include top abs errors
        if len(candidate_idxs) < (N+2):
            # take top abs err positions
            sorted_by_abs = sorted(range(len(errs)), key=lambda i: -abs(errs[i]))
            for idx in sorted_by_abs:
                if idx not in candidate_idxs:
                    candidate_idxs.append(idx)
                if len(candidate_idxs) >= (N+2):
                    break

        # select N+2 alternating points
        new_alt_idx = select_alternating_points(candidate_idxs, errs, N+2)

        # compute new_max_err
        new_E = max(abs(errs[i]) for i in new_alt_idx)

        if verbose:
            print(f"iter {iteration}: E={E:.12g} newE(alt)={new_E:.12g}")

        # check convergence: if change in E small, stop
        if last_E is not None and abs(new_E - last_E) <= tol * max(1.0, abs(last_E)):
            if verbose:
                print("Converged (tol).")
            # set final coefficients by solving with new_alt_idx one final time
            alt_idx = new_alt_idx
            # rebuild system and solve one last time
            A = []
            b = []
            for ii, idx in enumerate(alt_idx):
                tk = grid_t[idx]
                yk = ys[idx]
                Trow = [0.0]*(N+1)
                Trow[0] = 1.0
                if N >= 1:
                    Trow[1] = tk
                for j in range(2, N+1):
                    Trow[j] = 2.0 * tk * Trow[j-1] - Trow[j-2]
                A.append(Trow + [ (1.0 if (ii % 2 == 0) else -1.0) ])
                b.append(yk)
            if NP:
                import numpy as _np
                sol = _np.linalg.solve(_np.array(A,dtype=_np.float64), _np.array(b,dtype=_np.float64))
                c = [float(sol[i]) for i in range(N+1)]
                E = float(sol[N+1])
            else:
                sol = solve_linear_system_py(A,b)
                c = sol[:N+1]
                E = sol[N+1]
            return c, E, errs
        # update alternation points and iterate
        alt_idx = new_alt_idx
        last_E = new_E

    # if we reach max_iter, return last coefficients
    if verbose:
        print("Reached max iterations.")
    return c, E, errs

# ---------------------------
# Header emission
# ---------------------------
HEADER_TEMPLATE = r"""/*
  GENERATED CHEBYSHEV (REMEZ) HEADER
  --------------------------------
  degree = {degree}
  segments=1 (global Chebyshev over t∈[-1,1])
  sampling grid = {grid_n}
  max_iter = {max_iter}
  max_error (approx) = {max_err:.12g}

  This header evaluates a Chebyshev polynomial of degree {degree} in Q16.16
  on input x (uint32_t) that is interpreted as an angle across full uint32
  range mapping to t∈[-1,1].

  Function exported:
    // evaluate minimax Chebyshev polynomial in Q16.16
    static inline int32_t cheb_eval_q16(uint32_t x_uint32);

  Coefficients are stored as Q16.16 signed ints named CHEB_COEFF_Q16[k]
*/

#ifndef CHEB_REMEZ_HEADER_H
#define CHEB_REMEZ_HEADER_H

#include <stdint.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define CHEB_DEGREE {degree}
static const int32_t CHEB_COEFF_Q16[CHEB_DEGREE+1] = {{
{coeffs_array}
}};

/* Convert uint32 x in [0..2^32-1] -> t in [-1,1] as Q16.16 integer.
   Implementation here uses integer arithmetic:
     t = (2*x / UINT32_MAX) - 1
   We compute scaled = round( (2*x * 2^16) / UINT32_MAX )  then subtract 2^16.
*/
static inline int32_t xuint32_to_t_q16(uint32_t x) {{
    // Use 64-bit product for accuracy
    const uint64_t mul = (uint64_t)x * (uint64_t)(2u * (1u << 16)); // x * 2 * 2^16
    const uint64_t denom = {uint32_max:u};
    uint64_t scaled = mul / denom; // fits in 64-bit
    int64_t t_q16 = (int64_t)scaled - (1LL << 16);
    if (t_q16 > INT32_MAX) t_q16 = INT32_MAX;
    if (t_q16 < INT32_MIN) t_q16 = INT32_MIN;
    return (int32_t)t_q16;
}}

/* Clenshaw recurrence in Q16.16
   Evaluate Chebyshev series sum_{k=0..n} c_k T_k(t)
   where c_k are Q16 ints, t is Q16.
*/
static inline int32_t cheb_eval_q16(uint32_t x_uint32) {{
    if (CHEB_DEGREE < 0) return 0;
    int32_t t_q16 = xuint32_to_t_q16(x_uint32);
    // Clenshaw:
    int64_t b_kplus1 = 0;
    int64_t b_kplus2 = 0;
    for (int k = CHEB_DEGREE; k >= 0; --k) {{
        // temp = 2 * t * b_kplus1  (Q16 * Q16 -> Q32, >>15 to produce Q16 multiplied by 2)
        int64_t tmp = (int64_t)t_q16 * b_kplus1; // Q32
        int64_t two_t_b = tmp >> 15; // >>15 does (tmp/2^15) giving 2*t*b in Q16
        int64_t val = (int64_t)CHEB_COEFF_Q16[k] + two_t_b - b_kplus2;
        b_kplus2 = b_kplus1;
        b_kplus1 = val;
    }}
    // result = b_kplus1 - t*b_kplus2
    int64_t prod = (int64_t)t_q16 * b_kplus2; // Q32
    int64_t tb = prod >> 16; // Q16
    int64_t res = b_kplus1 - tb;
    if (res > INT32_MAX) return INT32_MAX;
    if (res < INT32_MIN) return INT32_MIN;
    return (int32_t)res;
}}

#ifdef __cplusplus
}}
#endif

#endif // CHEB_REMEZ_HEADER_H
"""

# ---------------------------
# Main CLI
# ---------------------------
def main():
    p = argparse.ArgumentParser(description="Generate Chebyshev minimax polynomial via discrete Remez; output header with Q16.16 coefficients.")
    p.add_argument("--in", "-i", dest="infile", help="CSV input file with 'x_uint32,y_float' rows. If omitted uses builtin target.")
    p.add_argument("--target", "-t", default="sin", help="Builtin target if --in omitted (sin/cos/tanh).")
    p.add_argument("--out", "-o", default="cheb_remez_trig.h")
    p.add_argument("--degree", "-d", type=int, default=7, help="Polynomial degree")
    p.add_argument("--grid", type=int, default=65536, help="Grid size for Lebesgue-style sampling (dense).")
    p.add_argument("--max-iter", type=int, default=40)
    p.add_argument("--tol", type=float, default=1e-12)
    p.add_argument("--verbose", action="store_true")
    args = p.parse_args()

    if args.infile:
        xs, ys = load_csv_samples(Path(args.infile))
        if len(xs) == 0:
            print("Input CSV empty or not found.", file=sys.stderr); sys.exit(1)
        # create grid_t from xs (map to t float)
        grid_t = [x_to_t_float(x) for x in xs]
        grid_y = ys[:]
    else:
        # generate default target on a dense grid
        gs = args.grid
        xs, ys = default_target_samples(args.target, gs)
        grid_t = [x_to_t_float(x) for x in xs]
        grid_y = ys

    # run Remez
    if args.verbose:
        print("Running Remez: degree=", args.degree, "grid=", len(grid_t))

    c, E, errs = remez_discrete(xs_t=None, ys=grid_y, degree=args.degree,
                                grid_t=grid_t, max_iter=args.max_iter,
                                tol=args.tol, verbose=args.verbose)

    # Convert Chebyshev coefficients (floats) -> Q16.16 ints
    qcoeffs = [int(round(ci * (1 << 16))) for ci in c]

    max_err = max(abs(e) for e in errs) if errs else 0.0

    # produce C header text
    coeffs_array_lines = ",\n".join("    %d" % (q & 0xFFFFFFFF if q >= 0 else (q & 0xFFFFFFFF)) for q in qcoeffs)
    header_text = HEADER_TEMPLATE.format(degree=args.degree, grid_n=len(grid_t),
                                        max_iter=args.max_iter, max_err=max_err,
                                        coeffs_array=coeffs_array_lines,
                                        uint32_max=UINT32_MAX_VAL)

    Path(args.out).write_text(header_text)
    print("Wrote header:", args.out)
    print("Degree:", args.degree, "Grid:", len(grid_t), "Max error approx:", max_err)

if __name__ == "__main__":
    main()
