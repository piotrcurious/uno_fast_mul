# uno_fast_mul
fast multiplication for Arduino uno using various tricks

Explanation — how this works (and the algebraic-geometry view)

The simple numeric idea

Multiplication in the positive reals is turned into addition by the logarithm:

a * b = 2^{ log2(a) + log2(b) }.

We approximate log2(x) with a fixed-point table and reconstruct 2^{y} via another table for the fractional part and a shift for the integer part.

The implementation steps:

1. Normalize v to a mantissa in [128..255] and an exponent e = floor(log2(v)). We use a fast msb lookup table to find e.


2. Use a mantissa→log2 table (log2_table[mant8]) to get log2(mant8) in Q8.8.


3. Combine: log2(v) ≈ (e - 7) + log2(mant8). Store that in Q8.8.


4. For product: add the two Q8.8 logs, split integer and fractional parts; use an exp2_table for the fractional exponent and shift for integer part.



Storing the tables in PROGMEM (flash) keeps RAM free; read_word_progmem shows how to LPM-read bytes on AVR quickly.

Why base-2 and the mantissa trick?

Using base-2 aligns perfectly with bit operations (shifts) on microcontrollers. Normalizing so the mantissa has its top bit set (i.e., 128..255) extracts most significant information into a small 8-bit index, so only 256 table entries are needed for good fractional resolution. The integer part becomes a single shift.

Algebraic-geometry foundations (brief, accurate, and useful)

At a conceptual algebraic level you can think about the multiplicative group of a field (or ring) and the additive group:

The positive reals (and many multiplicative groups) form an algebraic object often denoted  (the multiplicative group, the algebraic torus in 1D). The logarithm is a map transforming multiplicative structure into additive structure: it is a homomorphism from (R_{>0}, *) into (R, +) when defined (for reals via the real logarithm).

In formal algebraic geometry there is a closely related concept: the formal group law. The multiplicative formal group can be written with a formal logarithm and exponential that linearize multiplication to addition in a formal neighborhood. Practically, on an embedded MCU we implement discrete approximations of the logarithm/exponential (finite tables ≈ truncated Taylor/series), which is a computational analogue of the formal log/exp isomorphism between (formal) multiplicative and additive groups.

Thinking of data values as points on a 1-dimensional torus (a multiplicative group) and applying the log is like taking coordinates in the tangent (additive) space where composition (multiplication) becomes simpler (addition).


This viewpoint is helpful for generalizations: the log is a coordinate change on a group scheme that converts a non-linear operation into a linear one (where linear algebra tools apply).

Extensions using custom tables (practical ideas)

Because the method is a table-based coordinate change you can extend it in many ways:

Division: a / b = 2^{ log2(a) - log2(b) }. Implement by subtraction in Q8.8.

Powers/Roots: a^k → scale log by k (integer or rational). Roots are dividing the log.

Clipping / Saturation: clamp the integer part of the log (or saturate sum) before exponentiating to implement soft/hard caps on product magnitude.

Soft-clip / smooth limiting: apply a small LUT or polynomial to the log domain to implement smooth saturation (less aliasing than clipping in linear domain).

Per-channel gains / transforms: a multiplicative gain becomes an additive offset in log domain (log2(g * a) = log2(g) + log2(a)), so per-channel multipliers are cheap (just add constants).

Complex transforms: extend to fixed-point complex magnitudes + angle (polar): multiply magnitudes via logs and add phases. For complex multiplication, handle angles separately and magnitudes in log domain — this is efficient when magnitude dynamic range is a problem.

Custom precision: increase fractional table density (use 512 or 1024 entries) if more accuracy is required. Use PROGMEM blocks of uint16_t or uint32_t as needed.

Other bases / transforms: you can precompute tables that map to other group homomorphisms / transforms (e.g., log base e, or non-linear warps), then use the same add/exp trick in those coordinates.


Accuracy, speed, and tradeoffs

Accuracy depends on table resolution (we used 8 bits of fractional resolution, Q8.8). Typical relative error will be a few tenths of a percent for many inputs; extreme cases (very small mantissa rounding) produce larger relative error.

Speed is orders of magnitude faster than doing true floating-point on AVR (software FP) — typically a handful of table fetches + shifts vs hundreds of instructions for software FP.

Memory: PROGMEM tables cost flash. The included tables are compact: a 256-entry exp2_table (u16) is 512 bytes, similar for log2_table. The msb table is 256 bytes. Total about 1.3 KiB — well within Uno flash (32 KiB).

Range: this code handles unsigned 16-bit inputs and produces a 32-bit approximate product. You can adapt the method to fractional fixed-point inputs by changing normalization and table semantics.



---

If you want, I can:

Provide a higher-precision variant (Q12.4 or Q10.6) with a 512-entry fractional table for much smaller relative error.

Adapt this to signed inputs (handle sign separately) or to fixed-point fractional inputs like Q1.15.

Produce a bench that measures average error and cycles on your Uno.


Which extension would you like first? (I’ll give fully working code for it — pick precision, signed/unsigned, or fixed-point mode.)
