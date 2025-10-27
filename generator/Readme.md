Quick notes & how to use

The script is configurable via CLI flags (table sizes, Q formats, focal length for perspective, etc.). Example:

python generate_tables.py --out my_tables.h --sin-cos-size 1024 --sin-cos-q 14


Drop the generated header into your Arduino sketch and use pgm_read_word() / pgm_read_byte() or the inline-LPM helpers (from the earlier AVR example) to read the tables quickly.

Naming convention in the header: sin_table_q15, log2_table_q8, exp2_table_q8, perspective_scale_table_q8, sphere_theta_sin_q15, etc. Constants: CONST_PI_LOG_Q8, CONST_PI_SIN_Q15.

The generated tables are decimal literals for clarity (you can change fmt_c_array to emit hex if you prefer).


If you want, I can:

Add a small --emit-c option to create a .c + .h pair with proper extern declarations.

Add a helper that prints the worst-case/mean relative error for the log/exp-based multiply (so you can pick Q and sizes).

Produce an Arduino example .ino showing how to read and use these tables (fast msb+mantissa log → add → exp back) with the inline LPM reader.


Which of those would you like next?
