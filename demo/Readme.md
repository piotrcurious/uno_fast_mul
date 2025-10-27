Step 1 — generate the tables (command)

Put the generator generate_tables.py (the script I gave you) into a working folder and run:

# generate header+definitions in separate files:
python generate_tables.py --out arduino_tables --emit-c --gen-atan --gen-stereo

This writes:

arduino_tables.h (with extern declarations)

arduino_tables.c (definitions with PROGMEM)


Copy both into your Arduino sketch folder (same folder as the .ino) so the Arduino IDE compiles them with the sketch.

If you prefer a single header (no .c) use:

python generate_tables.py --out arduino_tables

That writes one header with definitions; still include that header in the sketch.

--gen-atan and --gen-stereo are optional; they produce additional tables useful for angle approximations or stereographic projection.



Explanation — how the demo uses the generator tables

1. Generate tables: you run the Python generator to produce arduino_tables.h and arduino_tables.c. Those files contain the following arrays (names shown in code):

msb_table (uint8_t[256])

log2_table_q8 (uint16_t[256]) — Q8.8 log2 entries

exp2_table_q8 (uint16_t[256]) — Q8.8 fractional exp2 entries

sin_table_q15, cos_table_q15 (int16_t[N]) — base-circle trig in Q15 used for rotation

perspective_scale_table_q8 — used to modulate per-row scale by depth

optionally atan_slope_table_q15 and stereo_radial_table_q12 if you asked for them


The sketch reads these arrays with fast LPM helpers READ_WORD/READ_BYTE.


2. Fast multiplication as addition of logs:

fast_log2_q8_8() computes an integer Q8.8 approximation of log2(v) using msb lookup + log2_table_q8.

fast_exp2_from_q8_8() reconstructs an integer from a Q8.8 log using exp2_table_q8 + shifts.

fast_log_mul_u16(a,b) simply computes exp2( log2(a)+log2(b) ) via the tables — this is used in scale_coord_fast() to multiply Q8.8-scale values by fast table lookup rather than slower integer long multiply or software FP.



3. Why use per-character pixel transforms (the rationale you asked for):

A font stored in PROGMEM (fixed character set) uses far less RAM than a full frame buffer.

You can transform each character individually (rotate / scale / perspective) by mapping each set pixel in the character bitmap to a final screen pixel; the transformed pixel is then plotted with drawPixel.

This is significantly lighter on RAM and scales well when character glyphs are small (5×7, etc.). The choice avoids needing a full framebuffer which is infeasible on an Uno.



4. Where the fast mul routine is used:

The most expensive operation for transforms is scaling coordinates by a per-character scale. The demo uses fast_log_mul_u16 in scale_coord_fast() to multiply coordinates (expressed in Q8.8) by a Q8.8 scale factor.

Rotation uses sin/cos table values (Q15) with integer multiplies because rotating signed coordinates with signed circular functions is easier and precise in fixed-point with direct integer multiply; this keeps the rotation numerically stable.





---

Possible improvements & tuning (suggestions)

More accurate fast multiply: increase LOG_Q or log/exp table sizes in the generator to reduce relative error. Use the generator CLI flags to change --log-q or increase --log-size if you want denser mantissa tables.

Signed fast-muls: the demo uses the fast mul only for magnitudes (abs). A signed-aware fast multiply wrapper could be added to handle signed Q-format multiplies directly.

Use ESP32 + TFT_eSPI: if you want much faster rendering, move the sketch to an ESP32 with TFT_eSPI and reuse the exact same PROGMEM tables (the generator works the same). I can port the sketch to TFT_eSPI and use DMA/pushColors for much greater throughput.

Soft clipping: in log domain you can apply smooth clipping by squashing the log-sum before exponentiating (useful if you want saturating scale without harsh cutoff).

More glyphs / variable-width fonts: expand font5x7 in PROGMEM or generate fonts offline (python) and include them as PROGMEM arrays.



---

If you want next

I can provide the version adapted for ESP32 + TFT_eSPI (much faster, hardware-accelerated SPI + DMA) — basically the same code but using tft.pushColor() or writeRect() to batch pixels.

I can improve the font generation: generate a full ASCII 5×7 table automatically with the generator and include it in the generated header.

I can add a small benchmark routine to measure the error distribution and average cycles of the fast_log_mul pipeline for your chosen log_q and table sizes (the generator can produce stats).

I can add a signed-aware fast multiply wrapper if you want to use log-domain multiplication for rotations/more signed transforms.
