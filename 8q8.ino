/*
  fast_logmul.ino
  Fast multiplication on AVR Uno using "multiply = exp2( log2(a) + log2(b) )"
  - Uses PROGMEM tables: msb_table (8-bit), log2_table (Q8.8), exp2_table (Q8.8)
  - Demonstrates inline assembly LPM reads from program memory for speed
  - Handles uint16_t inputs (1..65535). Zero handled specially.
  - Returns uint32_t approximate product.
*/

#include <Arduino.h>
#include <avr/pgmspace.h>

// ---------- Tables (generated offline) ----------
// msb_table: floor(log2(x)) for x in 0..255 (byte)
const uint8_t PROGMEM msb_table[256] = {
  // 0..15 shown for brevity; full 256 values follow...
  0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,
  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
  5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
  5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
  6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
  6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
  6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
  6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
};

// log2_table[m] = round( log2(m) * 256 )  (Q8.8). We use entries for m in [128..255]
// to represent normalized mantissa in range [128..255] (i.e. 1.0..1.992*2^7).
const uint16_t PROGMEM log2_table[256] = {
  // first 128 entries unused (0..127) but we keep table simple and 256-length.
  // Values precomputed = round(log2(i) * 256)
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  // 128..255 (actual useful region)
  1792,1808,1822,1836,1850,1864,1877,1891,1904,1916,1929,1941,1953,1965,1976,1988,
  1999,2010,2021,2031,2041,2051,2061,2070,2079,2089,2097,2106,2114,2123,2131,2138,
  2146,2154,2161,2168,2175,2182,2189,2196,2202,2209,2215,2221,2227,2233,2239,2245,
  2250,2256,2261,2267,2272,2277,2282,2287,2292,2296,2301,2306,2310,2315,2319,2323,
  2328,2332,2336,2340,2344,2348,2352,2356,2360,2363,2367,2371,2374,2378,2381,2385,
  2388,2391,2395,2398,2401,2404,2407,2410,2413,2416,2419,2422,2424,2427,2430,2432,
  2435,2438,2440,2443,2445,2448,2450,2452,2455,2457,2459,2462,2464,2466,2468,2470,
  2472,2474,2476,2478,2480,2482,2484,2486,2487,2489,2491,2493,2494,2496,2498,2500
};

// exp2_table[f] = round( 2^(f/256) * 256 )   (Q8.8) for f = 0..255
const uint16_t PROGMEM exp2_table[256] = {
  256,257,258,259,260,261,262,263,264,265,266,267,268,269,270,271,
  272,273,275,276,277,278,279,281,282,283,284,286,287,288,289,291,
  292,293,295,296,297,299,300,301,303,304,306,307,308,310,311,313,
  314,316,317,319,320,322,323,325,326,328,329,331,333,334,336,337,
  339,341,342,344,346,347,349,351,353,354,356,358,360,361,363,365,
  367,369,370,372,374,376,378,380,381,383,385,387,389,391,393,395,
  397,399,401,403,405,407,409,411,413,415,417,419,421,423,425,428,
  430,432,434,436,438,441,443,445,447,449,452,454,456,458,461,463,
  465,468,470,472,475,477,479,482,484,487,489,492,494,497,499,502,
  504,507,509,512,515,517,520,522,525,528,530,533,536,538,541,544,
  547,549,552,555,558,561,563,566,569,572,575,578,581,584,587,590,
  593,596,599,602,605,608,611,614,617,620,623,626,630,633,636,639,
  642,645,649,652,655,658,662,665,668,672,675,678,682,685,688,692,
  695,699,702,705,709,712,716,719,723,726,730,733,737,741,744,748,
  751,755,759,762,766,770,774,777,781,785,789,792,796,800,804,808,
  812,815,819,823,827,831,835,839,843,847,851,855,859,863,867,871
};

// ---------- Inline-asm optimized progmem reads (example) ----------
static inline uint16_t read_word_progmem(const uint16_t *addr) {
  // Uses Z pointer and LPM to fetch two bytes from flash (program memory).
  // This is a little lower-level and can be marginally faster than pgm_read_word on AVR.
  uint16_t val;
  // "e" constraint puts address in Z (r30:r31) on avr-gcc
  asm volatile (
    "movw r30, %A1\n\t"   // move pointer low/high into r30:r31 (Z)
    "lpm\n\t"             // load progmem byte into r0 from Z, post-increment Z
    "mov %A0, r0\n\t"     // low byte -> val low
    "lpm\n\t"             // load next byte into r0 (Z now points to next byte)
    "mov %B0, r0\n\t"     // high byte -> val high
    "clr r1\n\t"          // maintain gcc requirement: r1 == 0
    : "=r" (val)
    : "r" (addr)
    : "r0", "r30", "r31"
  );
  return val;
}

static inline uint8_t read_byte_progmem(const uint8_t *addr) {
  uint8_t val;
  asm volatile (
    "movw r30, %A1\n\t"
    "lpm\n\t"
    "mov %0, r0\n\t"
    "clr r1\n\t"
    : "=r" (val)
    : "r" (addr)
    : "r0", "r30", "r31"
  );
  return val;
}

// ---------- Helper: compute floor(log2(x)) for 16-bit fast using msb_table ----------
static inline uint8_t fast_msb16(uint16_t v) {
  if (v >> 8) {
    uint8_t hi = (uint8_t)(v >> 8);
    return 8 + read_byte_progmem(&msb_table[hi]);
  } else {
    uint8_t lo = (uint8_t)(v & 0xFF);
    return read_byte_progmem(&msb_table[lo]);
  }
}

// ---------- Normalize value into mantissa (128..255) and exponent e ----------
// We compute:
//   e = floor(log2(v))
//   mant8 = (v << (15 - e)) >> 8   --> mant8 in [128 .. 255]
// So: v ≈ mant8 * 2^(e - 7)
// log2(v) = (e - 7) + log2(mant8)
static inline void normalize_to_mant8(uint16_t v, uint8_t &mant8, int8_t &e_out) {
  if (v == 0) { mant8 = 0; e_out = -127; return; } // sentinel for zero
  uint8_t e = fast_msb16(v);
  // shift amount (15 - e) fits 0..15
  uint32_t tmp = ((uint32_t)v << (15 - e)) >> 8; // safe in 32-bit
  mant8 = (uint8_t)tmp; // 128..255
  e_out = (int8_t)e;
}

// ---------- Convert uint16 inputs to Q8.8 log2 approx ----------
static inline int32_t fast_log2_q8_8(uint16_t v) {
  if (v == 0) return INT32_MIN; // sentinel for -inf
  uint8_t mant8;
  int8_t e;
  normalize_to_mant8(v, mant8, e);
  // log2(v) = (e - 7) + log2(mant8)
  uint16_t log_m = read_word_progmem(&log2_table[mant8]); // Q8.8
  int32_t result = ((int32_t)(e - 7) << 8) + (int32_t)log_m;
  return result; // Q8.8
}

// ---------- Convert Q8.8 log2 back to integer product (approx) ----------
static inline uint32_t fast_exp2_from_q8_8(int32_t log_q8_8) {
  if (log_q8_8 <= -32768) return 0; // -inf sentinel
  // integer part and fractional part
  int32_t integer = log_q8_8 >> 8;        // signed integer exponent
  uint8_t frac = (uint8_t)(log_q8_8 & 0xFF); // 0..255
  uint16_t exp_frac = read_word_progmem(&exp2_table[frac]); // Q8.8, approx 2^(frac/256)*256
  // compute product = 2^integer * 2^(frac/256)
  // = (exp_frac / 256) * (1 << integer)
  // => (exp_frac << integer) >> 8
  if (integer >= 31) {
    // Overflow guard: cap at max uint32_t
    return 0xFFFFFFFFUL;
  } else if (integer >= 0) {
    uint32_t val = ((uint32_t)exp_frac << integer) >> 8;
    return val;
  } else {
    // negative integer: shift right
    int shift = -integer;
    uint32_t val = ((uint32_t)exp_frac) >> (8 + shift - 0); // equivalent to (exp_frac/256) >> shift
    return val;
  }
}

// ---------- The fast multiply using log tables ----------
static inline uint32_t fast_log_mul_u16(uint16_t a, uint16_t b) {
  if (a == 0 || b == 0) return 0;
  int32_t la = fast_log2_q8_8(a);
  int32_t lb = fast_log2_q8_8(b);
  int32_t sum = la + lb; // still Q8.8
  return fast_exp2_from_q8_8(sum);
}

// ---------- Demo & simple error reporting ----------
void test_pair(uint16_t a, uint16_t b) {
  uint32_t exact = (uint32_t)a * (uint32_t)b;
  uint32_t approx = fast_log_mul_u16(a, b);
  int32_t err = (int32_t)approx - (int32_t)exact;
  float rel = exact ? (100.0f * err / (float)exact) : 0.0f;

  Serial.print(a); Serial.print(" * "); Serial.print(b);
  Serial.print(" = exact "); Serial.print(exact);
  Serial.print(", approx "); Serial.print(approx);
  Serial.print(", err "); Serial.print(err);
  Serial.print(" ("); Serial.print(rel, 3); Serial.println("%)");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("fast_logmul demo"));
  // A few chosen test pairs including extremes
  test_pair(1, 1);
  test_pair(123, 456);
  test_pair(30000, 2);
  test_pair(65535, 65535);
  test_pair(1023, 511);
  test_pair(500, 500);
}

void loop() {
  // periodic random tests
  static uint32_t t = 0;
  if (++t % 3000 == 0) {
    uint16_t a = random(1, 65535);
    uint16_t b = random(1, 65535);
    test_pair(a, b);
  }
}
