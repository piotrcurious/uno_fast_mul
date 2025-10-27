/*
  TextTransformDemo.ino
  Arduino Uno demo: per-character per-pixel transforms using PROGMEM tables.
  - Requires: Adafruit_GFX, Adafruit_ILI9341
  - Requires generated arduino_tables.h / arduino_tables.c from the Python generator
  - Uses fast log-add-exp multiplication (log2_table_q8, exp2_table_q8, msb_table)
  - Small 5x7 bitmap font for uppercase letters (A..Z), space, digits optionally
*/

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// Pin definitions (modify to your wiring)
#define TFT_CS    10
#define TFT_DC     9
#define TFT_RST    8

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Include the generated tables (make sure arduino_tables.h / .c is in sketch folder)
#include "arduino_tables.h" // created by python generate_tables.py --out arduino_tables --emit-c

// --------------------------- Inline PROGMEM readers (fast LPM) ---------------------------
// These inline readers use Z pointer and LPM to fetch bytes/words from program memory.
// They are slightly faster than pgm_read_word for hot loops.

static inline uint8_t read_byte_progmem_u8(const uint8_t *addr) {
  uint8_t val;
  asm volatile (
    "movw r30, %A1\n\t"   // put pointer into Z
    "lpm\n\t"
    "mov %0, r0\n\t"
    "clr r1\n\t"
    : "=r" (val)
    : "r" (addr)
    : "r0", "r30", "r31"
  );
  return val;
}

static inline uint16_t read_word_progmem_u16(const uint16_t *addr) {
  uint16_t val;
  asm volatile (
    "movw r30, %A1\n\t"
    "lpm\n\t"
    "mov %A0, r0\n\t"
    "lpm\n\t"
    "mov %B0, r0\n\t"
    "clr r1\n\t"
    : "=r" (val)
    : "r" (addr)
    : "r0", "r30", "r31"
  );
  return val;
}

// Fallback macros in case you prefer pgm_read_word():
// #define READ_WORD(addr) pgm_read_word(addr)
// #define READ_BYTE(addr) pgm_read_byte(addr)
#define READ_WORD(addr) read_word_progmem_u16((const uint16_t*)(addr))
#define READ_BYTE(addr) read_byte_progmem_u8((const uint8_t*)(addr))

// --------------------------- Fast log/exp multiply pipeline (Q8.8 style) ---------------------------
// We assume the generator used log_q = 8 (Q8.8). If you changed it, update LOG_Q here.
#define LOG_Q 8
#define LOG_SCALE (1 << LOG_Q)

// Helper: msb for 16-bit using msb_table (0..255). Tables were generated as msb_table[256].
static inline uint8_t fast_msb16(uint16_t v) {
  if (v >> 8) {
    uint8_t hi = (uint8_t)(v >> 8);
    return 8 + READ_BYTE(&msb_table[hi]);
  } else {
    uint8_t lo = (uint8_t)(v & 0xFF);
    return READ_BYTE(&msb_table[lo]);
  }
}

// Normalize v into mant8 (128..255) and exponent e
static inline void normalize_to_mant8(uint16_t v, uint8_t &mant8, int8_t &e_out) {
  if (v == 0) { mant8 = 0; e_out = -127; return; }
  uint8_t e = fast_msb16(v);
  // shift so top bit of mantissa is in bit7 (128..255)
  uint32_t tmp = ((uint32_t)v << (15 - e)) >> 8;
  mant8 = (uint8_t)tmp;
  e_out = (int8_t)e;
}

// log2(v) in Q8.8
static inline int32_t fast_log2_q8_8(uint16_t v) {
  if (v == 0) return INT32_MIN;
  uint8_t mant8;
  int8_t e;
  normalize_to_mant8(v, mant8, e);
  uint16_t log_m = READ_WORD(&log2_table_q8[mant8]); // Q8.8
  int32_t result = ((int32_t)(e - 7) << LOG_Q) + (int32_t)log_m;
  return result; // Q8.8
}

// exp2 from Q8.8 back to integer (approx)
static inline uint32_t fast_exp2_from_q8_8(int32_t log_q8_8) {
  if (log_q8_8 <= INT32_MIN/2) return 0;
  int32_t integer = log_q8_8 >> LOG_Q;
  uint8_t frac = (uint8_t)(log_q8_8 & 0xFF);
  uint16_t exp_frac = READ_WORD(&exp2_table_q8[frac]); // Q8.8
  if (integer >= 31) return 0xFFFFFFFFUL;
  else if (integer >= 0) {
    uint32_t val = ((uint32_t)exp_frac << integer) >> LOG_Q;
    return val;
  } else {
    int shift = -integer;
    uint32_t val = ((uint32_t)exp_frac) >> (LOG_Q + shift - 0);
    return val;
  }
}

// Fast approximate multiply: a*b ~ exp2( log2(a) + log2(b) )
static inline uint32_t fast_log_mul_u16(uint16_t a, uint16_t b) {
  if (a == 0 || b == 0) return 0;
  int32_t la = fast_log2_q8_8(a);
  int32_t lb = fast_log2_q8_8(b);
  int32_t sum = la + lb;
  return fast_exp2_from_q8_8(sum);
}

// --------------------------- Small 5x7 font (uppercase A..Z + space + some symbols) ---------------------------
// 5 columns per character, stored as bytes (LSB top). Characters are in order:
// ' ' (space), 'A'..'Z', '0'..'9', and '.' ',' '!' '?'
// For brevity we include A-Z + space + few symbols. Expand as needed.
const uint8_t PROGMEM font5x7[] = {
  // space (0)
  0x00,0x00,0x00,0x00,0x00,
  // A
  0x7C,0x12,0x11,0x12,0x7C,
  // B
  0x7F,0x49,0x49,0x49,0x36,
  // C
  0x3E,0x41,0x41,0x41,0x22,
  // D
  0x7F,0x41,0x41,0x22,0x1C,
  // E
  0x7F,0x49,0x49,0x49,0x41,
  // F
  0x7F,0x09,0x09,0x09,0x01,
  // G
  0x3E,0x41,0x49,0x49,0x7A,
  // H
  0x7F,0x08,0x08,0x08,0x7F,
  // I
  0x00,0x41,0x7F,0x41,0x00,
  // J
  0x20,0x40,0x41,0x3F,0x01,
  // K
  0x7F,0x08,0x14,0x22,0x41,
  // L
  0x7F,0x40,0x40,0x40,0x40,
  // M
  0x7F,0x02,0x0C,0x02,0x7F,
  // N
  0x7F,0x04,0x08,0x10,0x7F,
  // O
  0x3E,0x41,0x41,0x41,0x3E,
  // P
  0x7F,0x09,0x09,0x09,0x06,
  // Q
  0x3E,0x41,0x51,0x21,0x5E,
  // R
  0x7F,0x09,0x19,0x29,0x46,
  // S
  0x46,0x49,0x49,0x49,0x31,
  // T
  0x01,0x01,0x7F,0x01,0x01,
  // U
  0x3F,0x40,0x40,0x40,0x3F,
  // V
  0x1F,0x20,0x40,0x20,0x1F,
  // W
  0x3F,0x40,0x38,0x40,0x3F,
  // X
  0x63,0x14,0x08,0x14,0x63,
  // Y
  0x07,0x08,0x70,0x08,0x07,
  // Z
  0x61,0x51,0x49,0x45,0x43,
  // '.' (dot)
  0x00,0x60,0x60,0x00,0x00,
  // ',' (comma)
  0x00,0x80,0x60,0x00,0x00,
  // '!' (bang)
  0x00,0x00,0x7D,0x00,0x00,
  // '?' (question)
  0x02,0x01,0x59,0x06,0x02
};
// Mapping function: index 0 -> ' ' (space), 1->'A', 2->'B', ... adapt for your text

// Helper to get font pointer for char; returns pointer to 5-byte column array in PROGMEM
const uint8_t* font_ptr_for_char(char c) {
  if (c == ' ') return font5x7 + 0;
  if (c >= 'A' && c <= 'Z') {
    int idx = 1 + (c - 'A'); // A -> offset 1
    return font5x7 + idx*5;
  }
  if (c == '.') return font5x7 + (27*5);
  if (c == ',') return font5x7 + (28*5);
  if (c == '!') return font5x7 + (29*5);
  if (c == '?') return font5x7 + (30*5);
  // default -> space
  return font5x7 + 0;
}

// --------------------------- Transform pipeline (per-character) ---------------------------
// We'll implement: translate, uniform scale, rotate, perspective scale.
// For scale multiplications we use fast_log_mul_u16; rotation uses sin/cos table (Q15) with integer multiply.

// Access sin/cos base table generated (sin_table_q15 / cos_table_q15) with size sin_cos_size (default 512)
extern const int16_t PROGMEM sin_table_q15[]; // declared in arduino_tables.h
extern const int16_t PROGMEM cos_table_q15[]; // declared in arduino_tables.h
#define SIN_COS_SIZE 512
#define SIN_COS_Q 15
#define SIN_SCALE (1<<SIN_COS_Q)

// Read sin/cos (index wraps)
static inline int16_t get_sin_q15(uint16_t idx) {
  idx %= SIN_COS_SIZE;
  // read as word
  return (int16_t)READ_WORD(&sin_table_q15[idx]);
}
static inline int16_t get_cos_q15(uint16_t idx) {
  idx %= SIN_COS_SIZE;
  return (int16_t)READ_WORD(&cos_table_q15[idx]);
}

// Utility: convert a float angle_radians to table index
static inline uint16_t angle_to_index(float angle) {
  // map angle in radians to index in [0..SIN_COS_SIZE-1]
  // angle normalized to [0..2pi)
  while (angle < 0) angle += 2.0f * PI;
  while (angle >= 2.0f * PI) angle -= 2.0f * PI;
  return (uint16_t)((angle / (2.0f * PI)) * (float)SIN_COS_SIZE);
}

// Small helper to perform signed multiply with Q formats (sin/cos are Q15)
static inline int32_t mul_q15_i16(int16_t a_q15, int16_t b_q15) {
  // result is Q30; shift down to Q15 return Q15-like scaled result if needed.
  return ((int32_t)a_q15 * (int32_t)b_q15) >> SIN_COS_Q;
}

// Use fast_log_mul for positive scale multiplies (scale in Q8.8, coordinate in Q8.8)
static inline uint32_t scale_coord_fast(uint16_t coord_q8_8, uint16_t scale_q8_8) {
  // Both are positive unsigned Q8.8; convert to integers and multiply using fast_log_mul.
  // Note: fast_log_mul expects raw integers, produces integer approximate product.
  // We'll pass the raw 16-bit integer (coord) and scale as integer; result is approx coord*scale.
  // Since both are Q8.8, the true real product has Q16.16; we return Q8.8 by shifting >>8.
  uint32_t prod = fast_log_mul_u16(coord_q8_8, scale_q8_8);
  // prod approximates (coord * scale); but since both were Q8.8, we shift down 8 bits to keep Q8.8
  return (prod >> LOG_Q);
}

// --------------------------- Demo parameters ---------------------------
const char* rows[] = {
  "HELLO WORLD",
  "FAST LOG MUL DEMO",
  "ARITHMETIC TRANSFORMS"
};
const uint8_t ROW_COUNT = sizeof(rows)/sizeof(rows[0]);

// Screen layout
const int16_t SCREEN_W = 240;
const int16_t SCREEN_H = 320;
const uint16_t CHAR_W = 6; // 5px + 1px spacing
const uint16_t CHAR_H = 8; // 7px + 1 spacing

float global_angle = 0.0f; // rotation angle in radians
uint16_t global_frame = 0;

// --------------------------- Render one frame ---------------------------
void render_frame() {
  tft.fillScreen(ILI9341_BLACK);

  // for each row
  for (uint8_t r = 0; r < ROW_COUNT; ++r) {
    const char* s = rows[r];
    uint16_t text_len = strlen(s);

    // baseline position (centered horizontally)
    int16_t baseline_x = SCREEN_W / 2; // we'll translate chars around this
    int16_t baseline_y = 30 + r * 90;  // space rows out

    // per-row depth-based perspective (simulate different Z)
    // We'll use the generated perspective table (perspective_scale_table_q8)
    // Map row index to perspective table index:
    uint16_t persp_idx = (uint16_t)((r * 1.0f) / (ROW_COUNT - 1) * (255));
    uint16_t row_persp_q8 = READ_WORD(&perspective_scale_table_q8[persp_idx]); // Q8.8

    // character-level loop
    for (uint16_t ci = 0; ci < text_len; ++ci) {
      char ch = s[ci];
      const uint8_t* fontp = font_ptr_for_char(ch);

      // char local origin (character center)
      int16_t char_origin_x = baseline_x - (text_len * CHAR_W)/2 + ci * CHAR_W + CHAR_W/2;
      int16_t char_origin_y = baseline_y;

      // per-char scale varies slightly with index and time (simulate wave)
      // scale_base in Q8.8 (1.0 -> 256)
      float sbase = 0.7f + 0.6f * (0.5f + 0.5f * sin(global_frame * 0.03f + ci*0.5f));
      uint16_t scale_q8 = (uint16_t)round(sbase * (1<<LOG_Q)); // Q8.8

      // combine scale with perspective scale via fast log multiply.
      uint32_t combined_scale_q8 = (scale_coord_fast(scale_q8, row_persp_q8)); // Q8.8

      // rotation angle per char
      float angle = global_angle + 0.05f * sin(ci * 0.4f + global_frame * 0.02f);
      uint16_t aidx = angle_to_index(angle);
      int16_t cos_q15 = get_cos_q15(aidx);
      int16_t sin_q15 = get_sin_q15(aidx);

      // For each source pixel in 5x7 font, transform
      // font columns are bytes: bit0 top, bit6 bottom (we use y=0..6)
      for (uint8_t col = 0; col < 5; ++col) {
        uint8_t colbyte = READ_BYTE(fontp + col);
        for (uint8_t rowbit = 0; rowbit < 7; ++rowbit) {
          if (colbyte & (1 << rowbit)) {
            // source coordinate relative to char center (Q8.8)
            int16_t sx = (int16_t)col - 2; // range -2..+2
            int16_t sy = (int16_t)rowbit - 3; // range -3..+3

            // convert to Q8.8
            int16_t sx_q8 = sx << LOG_Q;
            int16_t sy_q8 = sy << LOG_Q;

            // apply uniform scale: scaled = fast_mul(|coord|, scale)
            uint16_t sx_abs = (uint16_t)abs(sx_q8);
            uint16_t sy_abs = (uint16_t)abs(sy_q8);
            uint32_t sx_scaled_q8 = scale_coord_fast(sx_abs, combined_scale_q8); // Q8.8
            uint32_t sy_scaled_q8 = scale_coord_fast(sy_abs, combined_scale_q8); // Q8.8
            int32_t sx_scaled_signed = (sx_q8 < 0) ? -((int32_t)sx_scaled_q8) : (int32_t)sx_scaled_q8;
            int32_t sy_scaled_signed = (sy_q8 < 0) ? -((int32_t)sy_scaled_q8) : (int32_t)sy_scaled_q8;

            // rotation: use sin/cos Q15. Multiply Q8.8 coords by Q15 sin/cos -> (Q8.8 * Q15)>>15 gives Q8.8 result
            int32_t rx_q8 = ( (sx_scaled_signed * (int32_t)cos_q15) - (sy_scaled_signed * (int32_t)sin_q15) ) >> SIN_COS_Q;
            int32_t ry_q8 = ( (sx_scaled_signed * (int32_t)sin_q15) + (sy_scaled_signed * (int32_t)cos_q15) ) >> SIN_COS_Q;

            // final screen coordinates (add origin, convert Q8.8 -> int)
            int16_t fx = char_origin_x + (int16_t)(rx_q8 >> LOG_Q);
            int16_t fy = char_origin_y + (int16_t)(ry_q8 >> LOG_Q);

            // draw pixel if inside screen bounds
            if (fx >= 0 && fx < SCREEN_W && fy >= 0 && fy < SCREEN_H) {
              tft.drawPixel(fx, fy, ILI9341_WHITE);
            }
          } // if bit set
        } // rowbit
      } // col
    } // char loop
  } // row loop
}

// --------------------------- Setup & loop ---------------------------
void setup() {
  Serial.begin(115200);
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);
  randomSeed(analogRead(A0));
}

void loop() {
  render_frame();
  // advance animation
  global_angle += 0.01f;
  global_frame++;
  delay(80);
}
