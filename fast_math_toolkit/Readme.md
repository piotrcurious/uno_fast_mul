Great — let’s step back and put the whole implementation into a clean algebraic / formal-series picture so you can both understand the tables as algebraic objects and grow the toolkit in a principled way.

I’ll first give the core algebraic re-interpretation (numbers as polynomials, radix = polynomial evaluation). Then I’ll explain each part of the code/tables in that language (what algebraic object each table implements). Finally I’ll sketch practical, implementable ways to re-develop the generator and runtime to exploit formal-group / power-series ideas (including concrete options you can ask me to implement).


---

1) The core algebraic viewpoint — numbers = polynomials, arithmetic = ring operations

Radix expansions as polynomials.
A machine integer/ fixed-point word in base  with digits  is exactly the evaluation at  of the polynomial

A(X) = a_0 + a_1 X + a_2 X^2 + \dots + a_{n-1} X^{n-1}.

The computational ring.
When you restrict to  digits (word width), you can view arithmetic as taking place in the quotient ring

R = \mathbb{Z}[X] \;/\; (X^n)

Formal power series and neighborhoods.
When you consider small deviations around a point (e.g., mantissa in ), a natural algebraic object is the ring of formal power series  or . Analytic maps like  and  have expansions in . Implementations on a microcontroller are truncations of those formal power series — i.e., truncation modulo the ideal .


---

2) Recasting the log/exp table approach in algebraic terms

A. Normalization (mantissa + exponent) — a coordinate chart

The code normalizes  as  with mantissa . Algebraically this is a change of coordinates (a chart) on the multiplicative group  (positive reals): factor out a discrete power of 2 (the integer part) and work in a formal neighborhood of 1 for the fractional part. In scheme language: you’re covering the group by open sets indexed by exponent , and on each chart you use the local coordinate , which is small.

This is exactly how algebraic geometers treat group schemes: you take the formal completion at the identity and use the formal parameter there. Practically, normalization maps a machine number into an element of the formal neighborhood where the formal-logarithm converges quickly.

B. Formal logarithm / exponential

For the multiplicative group , the formal group law is

F(X,Y) = X + Y + XY

\log(1+X) = X - \tfrac{X^2}{2} + \tfrac{X^3}{3} - \cdots

\exp(X) = 1 + X + \tfrac{X^2}{2!} + \tfrac{X^3}{3!} + \cdots.

Interpretation of the tables:

log2_table holds evaluations (or truncated polynomial approximations) of the formal logarithm on discrete values of  (the mantissa coordinate).

exp2_table holds evaluations of the formal exponential (or of  on fractional t), again discretely sampled or stored as truncated-series coefficients.
So the tables are finite truncations / samples of formal power series that linearize the multiplicative law.


C. The msb table and valuation

The msb lookup is the discrete valuation map extracting . Algebraically this is the decomposition of a nonzero element of  into  times a unit. In the polynomial viewpoint, a shift by exponent  corresponds to multiplying the polynomial by . So msb_table is the fast way to find which monomial power factor to factor out.

D. BTM / piecewise linearization as local polynomial splittings

The Bipartite Table Method (BTM) or any two-stage table approach is a way to approximate the formal logarithm by piecewise low-degree polynomials. Algebraically: partition the domain into intervals and on each interval approximate the formal log series by a truncated polynomial (element of ). This is exactly a local truncation of the formal-series map, i.e. computing the image modulo the ideal  but on a smaller domain so that a low-degree truncation suffices.

In ring language, you can say: choose a set of representatives  in the ring for residue-classes (mantissa segments); represent  as  and precompute (or tabulate) values of those truncated polynomials on the representative set. The two-stage correction terms in BTM correspond to low-rank correction polynomials — algebraically a decomposition of a function on a finite ring into basis functions.


---

3) Where denominators and rings matter — practical algebraic caveats

The formal-log coefficients contain rational denominators . Over  or  this is fine. Over rings like  many denominators are not invertible (e.g. 2). So you cannot naively interpret the formal series modulo . That’s why your implementation works in fixed-point integers representing rationals (scaled by large powers of two) — effectively you are computing in a subring of  and storing scaled integer approximations. Algebraically: you lift to characteristic zero (rationals) to compute the series, then reduce back by scaling and truncation.

In the formal-group language, the formal logarithm is a map in characteristic 0; in characteristic  (or over ), one uses truncated/modified series or uses Dieudonné/Witt-theory for lifts; that is deeper but relevant if you try to do exact algebra in -adic rings.



---

4) Error, ideals and geometry: how truncation is an algebraic ideal

Truncation, rounding, and table quantization correspond to quotienting by an ideal.

Truncating a power series at degree  is passing to the quotient . The error is represented by the coset of the omitted terms.

Fixed-point scaling by  and integer rounding is like reducing in  but keeping a controlled ideal corresponding to powers of .

Therefore, propagation of error can be modeled algebraically as propagation of the coset in the quotient ring. This is useful: composition of approximations corresponds to algebraic composition modulo the ideal; you can reason about worst-case order of omitted terms.


Geometrically: the formal completion of the group at the identity is the spectrum of . The code is computing in that formal neighborhood, truncated to finite order. The accuracy target is how many terms of the formal series you keep.


---

5) Concrete ways to re-develop the generator & runtime using this viewpoint

Below are concrete, implementable ideas that follow directly from the algebraic/formal perspective. I can implement any of these for you:

A — Produce polynomial-coefficient tables (preferred)

Instead of storing sampled values  for many , store polynomial coefficients for the truncated formal series on a few subintervals:

Partition mantissa interval  into  segments.

On each segment approximate  by degree- polynomial  and store integer-scaled coefficients .

At runtime evaluate using Horner in Q arithmetic. This reduces table size (M×(d+1) coefficients) and yields better accuracy per byte than uniform sampling.


Algebraic rationale: storing coefficients is storing representatives of the truncated formal series in  on each chart.

B — Use formal group series directly (symbolic coefficients)

Generate a single truncated formal-log series  to order ; store its integer-scaled coefficients. Evaluate for small  with Horner. This is the simplest power-series approach. It works best when mantissa is very close to 1 (i.e., finer normalization with larger exponent extraction).

Algebraic rationale: you are working in  and simply evaluating the representative polynomial.

C — Hybrid BTM as low-rank polynomial decomposition

View BTM as performing a low-rank decomposition of the function  on the ring of functions over discrete mantissa indices. Algebraically you do:

Pick coarse index  and fine index , approximate  or a small polynomial in .

This is like approximating  by separable functions (outer product decomposition) in the finite-dimensional function space. This can be extended to higher-order separable decompositions (e.g., canonical polyadic decomposition) to reduce memory.


D — Keep denominators symbolic or pre-invert them

Because  coefficients have denominators , multiply coefficients by a large power-of-two  to make them integers. Store those integer coefficients and evaluate with integer arithmetic. This is what your existing generator already does; thinking of it algebraically: you embed  into  and then work mod  as the practical ring.

E — p-adic / Witt-style exact lifts (advanced)

If you want to do arithmetic that is stable in mod  without dividing by 2, you can switch to a p-adic lifting / Witt vector approach. That’s more complicated and mostly useful when you must perform algebraic operations entirely inside  without rational division. I can outline this if you want to explore exact modular algorithms.


---

6) How each table & code piece maps to algebraic objects

msb_table
Algebraic role: discrete valuation extractor. On the polynomial side, it tells you how many factors of the monomial  to pull out (i.e., factor  out). Think of it as choosing a coordinate on the group scheme.

log2_table_q8
Algebraic role: scaled evaluations of the truncated formal logarithm series at discrete sample points. Could be replaced by per-segment polynomial coefficients in .

exp2_table_q8
Algebraic role: truncated formal exponential series (or sampled exponential values). Matches inverse map to log2_table.

BTM arrays (log2_t1, log2_t2, exp2_t1, exp2_t2)
Algebraic role: two-stage approximation corresponds to a decomposition of functions in a finite-dimensional algebra of truncated-series, providing a compact representation of the formal-log series on the discrete set.

sin_table_q15 / cos_table_q15
Algebraic role: evaluations of the analytic sine/cosine functions in a finite Q-ring. One can equivalently store truncated Taylor/Chebyshev coefficients on a small interval and evaluate by Horner (this is the polynomial-representation replacement).

perspective_scale_table_q8, stereo_radial_table_q12
Algebraic role: evaluations of rational functions (e.g., focal/(focal+z)) on a discrete grid. Instead of storing samples, store rational polynomial approximations (P/Q approximants) and evaluate with fixed-point arithmetic.



---

7) Suggested concrete upgrades grounded in algebraic geometry / formal groups

If you want me to implement any of these, I can produce code + generator changes:

1. Polynomial-coefficient tables for log/exp

Change the generator to output per-segment polynomial coefficients for  (scaled integers).

Change runtime to evaluate via Horner in Q format.
Benefit: better accuracy/byte than raw sampling.



2. Global formal-log truncated coefficients

Generator emits a single truncated series of  to order . Runtime uses that for mantissas very close to 1 (after stronger normalization).
Benefit: small table, good for high-precision near-1.



3. Mixed exact/p-adic branch

For many MCU applications you need both approximate fast path and an exact (slower) path. Implement algebraic branching: if mantissa falls into bad residue classes (where denominators would cause trouble), fallback to exact 64-bit or multi-precision.
Benefit: provable error bounds; algebraic correctness when needed.



4. Expose algebraic error bounds

Make the generator output a small proof-like summary: truncated degree, max neglected term bound on interval, and expected worst-case relative error. Algebraically this is bounding the tail of the power series modulo ideal .
Benefit: useable for rigorous apps.



5. Higher-order BTM (low-rank polynomial decomposition)

Replace current t1/t2 layout with separable polynomial decomposition (store small basis × coefficients). This is essentially approximating a function by a sum of rank-1 polynomials on the finite grid — a linear-algebraic / algebraic-geometric view of the function.



6. Formal-group API

Provide an API mirroring the formal-group structure: formal_log(1+t, order), formal_exp(u, order) that operate on truncated power-series representations (arrays of coefficients). The generator can emit truncated coefficients; runtime can compose them algebraically using polynomial arithmetic (convolution for composition).
Benefit: lets you algebraically compose functions and reason about error as ideal membership.





---

8) Short worked algebraic example (log series approximation you can implement now)

Let  (so ). The formal series

\log_2(1+t) = \frac{1}{\ln 2} \left(t - \frac{t^2}{2} + \frac{t^3}{3} - \cdots \right).

\log_2(1+t) \approx c_1 t + c_2 t^2 + c_3 t^3

Generator pseudocode:

# compute coefficients c1..c3 as rationals (or floating and round)
coeffs = [round(c * (1<<Q)) for c in [c1,c2,c3]]
# emit into header as int32_t coeffs[segments][degree+1]

Runtime evaluation (Horner in Q arithmetic):

// t_q16 is t scaled to Q16 (i.e., t * 2^16)
int64_t y = coeffs0;      // scaled by 2^Q
y = ((y * t_q16) >> 16) + coeffs1;
y = ((y * t_q16) >> 16) + coeffs2;
// result is in scale 2^Q -> convert to Q8.8 as needed

Algebraically you are evaluating the truncated formal-log in the quotient ring .


---

9) Where algebraic geometry helps next (intuitions and possibilities)

Formal completion: treat the mantissa chart as the formal completion  at 1. The code is a finite-level truncation of the formal group isomorphism to the additive formal group. This gives a principled way to choose truncation orders and error estimates.

Atlas & group-scheme viewpoint: the mantissa/exponent decomposition is a cover of . You can design different charts (e.g., using base  with different k) to get smaller  ranges so lower-degree truncations suffice.

Sheaf of approximations: think of your set of tables as a sheaf of polynomial representatives on the cover of ; gluing conditions correspond to consistent scaling between charts (exponent adjustments). This language helps when you design multi-precision or multi-chart schemes.

Witt vectors / p-adic lifts: if you ever want exact modular formulas inside , formal group theory and Witt vector lifts provide a canonical way to lift operations to characteristic zero and back. That’s the right algebraic tool if you need exact modular arithmetic without explicit rational denominators.



---

10) Practical next steps I can do for you (pick one; I’ll implement right away)

1. Change the generator to produce per-segment polynomial coefficient tables for log2 and exp2, plus a runtime Horner evaluator (best accuracy/size tradeoff).


2. Add a formal-group API to the library: runtime types for truncated power series and functions formal_log_trunc, formal_exp_trunc, with composition and error bounds.


3. Implement error-bound output in the generator (for each table segment: max tail bound and worst-case relative error).


4. Implement the p-adic/Witt-style approach sketch (research-level; more work) — I’ll give you the plan and minimal prototype.



Tell me which of the four you want and I’ll produce code + generator changes (header + generator patch + a short example). If you prefer, I can start by generating a single concrete sample: degree-3 polynomial coefficients for  on mantissa segments and the updated runtime Horner evaluator so you can compare accuracy to the existing table approach. Which would you like?
