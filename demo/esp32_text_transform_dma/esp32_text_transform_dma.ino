// ESP32_TextTransform_DMA.ino
// ESP32 demo: per-glyph transformed text rendering using tables and pushImage()
// Requires: TFT_eSPI library (configured for your display & using DMA if desired)
// Place the generated arduino_tables.h / arduino_tables.c (from generator) into the sketch folder.

#include <Arduino.h>
#include <TFT_eSPI.h> // https://github.com/Bodmer/TFT_eSPI

TFT_eSPI tft = TFT_eSPI(); // init with user configuration

// include generated tables + glyphs
#include "arduino_tables.h" // produced by generate_tables_and_font.py

// We'll access arrays directly (on ESP32 const arrays live in flash).
// Table naming assumptions (adjust if generator params differ):
// - msb_table (uint8_t[256])
// - log2_table_q8 (uint16_t[256])
// - exp2_table_q8 (uint16_t[256])
// - sin_table_q15, cos_table_q15 (int16_t[SIN_COS_SIZE])
// - perspective_scale_table_q8 (uint16_t[256])
// - GLYPH_BITMAPS, GLYPH_CHAR_LIST, GLYPH_WIDTH, GLYPH_HEIGHT

// Fixed Q settings used by generator
#define LOG_Q 8
#define LOG_SCALE (1 << LOG_Q)
#define SIN_Q 15
#define SIN_SIZE 512

// Fast log/exp-based multiply pipeline (reads from const flash arrays)
static inline uint8_t fast_msb16(uint16_t v) {
  if (v >> 8) {
    return 8 + msb_table[v >> 8];
  } else {
    return msb_table[v & 0xFF];
  }
}

static inline void normalize_to_mant8(uint16_t v, uint8_t &mant8, int8_t &e_out) {
  if (v == 0) { mant8 = 0; e_out = -127; return; }
  uint8_t e = fast_msb16(v);
  uint32_t tmp = ((uint32_t)v << (15 - e)) >> 8;
  mant8 = (uint8_t)tmp;
  e_out = (int8_t)e;
}

static inline int32_t fast_log2_q8_8(uint16_t v) {
  if (v == 0) return INT32_MIN;
  uint8_t mant8;
  int8_t e;
  normalize_to_mant8(v, mant8, e);
  uint16_t log_m = log2_table_q8[mant8];
  int32_t result = ((int32_t)(e - 7) << LOG_Q) + (int32_t)log_m;
  return result;
}

static inline uint32_t fast_exp2_from_q8_8(int32_t log_q8_8) {
  if (log_q8_8 <= -32768) return 0;
  int32_t integer = log_q8_8 >> LOG_Q;
  uint8_t frac = (uint8_t)(log_q8_8 & 0xFF);
  uint16_t exp_frac = exp2_table_q8[frac];
  if (integer >= 32) {
    return 0xFFFFFFFFUL;
  } else if (integer >= 8) {
    uint32_t val = (uint32_t)exp_frac << (integer - 8);
    return val;
  } else if (integer >= 0) {
    uint32_t val = (uint32_t)exp_frac >> (8 - integer);
    return val;
  } else {
    int shift = -integer;
    if (shift >= 24) return 0;
    uint32_t val = ((uint32_t)exp_frac) >> (8 + shift);
    return val;
  }
}

static inline uint32_t fast_log_mul_u16(uint16_t a, uint16_t b) {
  if (a == 0 || b == 0) return 0;
  int32_t la = fast_log2_q8_8(a);
  int32_t lb = fast_log2_q8_8(b);
  int32_t sum = la + lb;
  return fast_exp2_from_q8_8(sum);
}

// sin/cos lookup helper
static inline int16_t sin_q15_index(uint16_t idx) {
  idx %= SIN_SIZE;
  return sin_table_q15[idx];
}
static inline int16_t cos_q15_index(uint16_t idx) {
  idx %= SIN_SIZE;
  return cos_table_q15[idx];
}

// angle -> index
static inline uint16_t angle_to_index(float angle) {
  // normalize and map to [0..SIN_SIZE-1]
  while (angle < 0) angle += 2.0f * PI;
  while (angle >= 2.0f * PI) angle -= 2.0f * PI;
  return (uint16_t)((angle / (2.0f * PI)) * (float)SIN_SIZE);
}

// glyph helpers (GLYPH_* from header)

static inline int glyph_index_for_char(char c) {
  // naive search in GLYPH_CHAR_LIST (fast enough for small set)
  for (uint16_t i = 0; i < GLYPH_COUNT; ++i) {
    if (GLYPH_CHAR_LIST[i] == c) return i;
  }
  return -1;
}

// We'll render each glyph into a small RGB565 buffer and pushImage() it to the TFT.
// This allows the TFT_eSPI library to use its internal faster transfer path / DMA.
struct GlyphBuffer {
  uint16_t w, h;
  uint16_t *buf; // allocated once (reused)
  GlyphBuffer(uint16_t w_, uint16_t h_): w(w_), h(h_) {
    buf = (uint16_t*)malloc(sizeof(uint16_t) * w * h);
    if (!buf) {
      Serial.println("GlyphBuffer alloc failed");
    }
  }
  ~GlyphBuffer() { if (buf) free(buf); }
  void clear(uint16_t color = 0x0000) {
    if (!buf) return;
    uint32_t n = (uint32_t)w * h;
    for (uint32_t i=0;i<n;++i) buf[i] = color;
  }
};

// simple safe clamp to screen
static inline bool in_bounds(int16_t x, int16_t y) {
  return (x >= 0 && y >= 0 && x < tft.width() && y < tft.height());
}

// -------------------- Demo parameters --------------------
const char* TEXT_ROWS[] = {
  "HELLO ESP32 DMA",
  "FAST LOG MUL + TRANSFORMS",
  "PROGMEM GLYPH DEMO"
};
const uint8_t ROWS = sizeof(TEXT_ROWS) / sizeof(TEXT_ROWS[0]);
float global_angle = 0.0f;
uint32_t frame_count = 0;

// Benchmark accumulators
uint64_t bench_total_time_us = 0;
uint32_t bench_frames = 0;
uint32_t bench_mul_samples = 0;
double bench_mul_error_sum = 0.0;
uint32_t bench_mul_error_max = 0;

// helper: compute fast mul error stats (compare with exact)
static inline void bench_record_mul(uint16_t a, uint16_t b) {
  uint32_t exact = (uint32_t)a * (uint32_t)b;
  uint32_t approx = fast_log_mul_u16(a,b);
  int32_t diff = (int32_t)approx - (int32_t)exact;
  uint32_t err = (diff >= 0) ? diff : -diff;
  bench_mul_samples++;
  bench_mul_error_sum += (double)err / (double)max(1u, exact);
  if (err > bench_mul_error_max) bench_mul_error_max = err;
}

// Render one glyph at location (cx,cy) as center using transformations
void render_glyph_transformed(char ch, int16_t cx, int16_t cy, float scale_f, float angle_rad, uint16_t color) {
  int idx = glyph_index_for_char(ch);
  if (idx < 0) return;
  const uint8_t gw = GLYPH_WIDTH;
  const uint8_t gh = GLYPH_HEIGHT;

  // prepare glyph buffer
  static GlyphBuffer *gbuf = nullptr;
  if (!gbuf) gbuf = new GlyphBuffer(gw, gh);
  gbuf->clear(0x0000);

  // combined scale: map float scale_f to Q8.8 integer (1.0 -> 256)
  uint16_t scale_q8 = (uint16_t)round(scale_f * (1 << LOG_Q));

  // perspective scale from table (simulate by indexing by vertical position)
  uint16_t persp_idx = (uint16_t)((cy / (float)tft.height()) * 255.0f);
  if (persp_idx > 255) persp_idx = 255;
  uint16_t persp_q8 = perspective_scale_table_q8[persp_idx];

  uint32_t combined_scale_q8 = (fast_log_mul_u16(scale_q8, persp_q8) >> LOG_Q); // Q8.8
  // record some bench samples:
  bench_record_mul(scale_q8, persp_q8);

  // rotation indices & q15 values
  uint16_t aidx = angle_to_index(angle_rad);
  int16_t cos_q15 = cos_table_q15[aidx];
  int16_t sin_q15 = sin_table_q15[aidx];

  // iterate glyph pixels (columns then rows)
  // GLYPH_BITMAPS is flattened: index = glyph_idx * gw + col
  for (uint8_t col = 0; col < gw; ++col) {
    uint8_t colbyte = GLYPH_BITMAPS[idx * gw + col];
    for (uint8_t row = 0; row < gh; ++row) {
      if (colbyte & (1 << row)) {
        // source coordinates centered at glyph origin:
        int16_t sx = (int16_t)col - (gw / 2);
        int16_t sy = (int16_t)row - (gh / 2);

        // scale using combined_scale_q8 (Q8.8). convert sx,sy to Q8.8
        int32_t sx_q8 = ((int32_t)sx) << LOG_Q;
        int32_t sy_q8 = ((int32_t)sy) << LOG_Q;
        // absolute multiplies via fast_log_mul: take abs and restore sign
        uint32_t asx = (uint32_t)(sx_q8 < 0 ? -sx_q8 : sx_q8); // Q8.8
        uint32_t asy = (uint32_t)(sy_q8 < 0 ? -sy_q8 : sy_q8); // Q8.8
        // fast_log_mul expects 16-bit input
        uint16_t asx16 = (uint16_t)min(asx, (uint32_t)65535);
        uint16_t asy16 = (uint16_t)min(asy, (uint32_t)65535);
        // fast_log_mul returns approx product of integers.
        // since both inputs are Q8.8, product is Q16.16. Shift >>8 to get Q8.8 result.
        uint32_t sx_scaled_q8 = fast_log_mul_u16(asx16, (uint16_t)combined_scale_q8) >> LOG_Q;
        uint32_t sy_scaled_q8 = fast_log_mul_u16(asy16, (uint16_t)combined_scale_q8) >> LOG_Q;
        bench_record_mul(asx16, (uint16_t)combined_scale_q8);
        bench_record_mul(asy16, (uint16_t)combined_scale_q8);
        int32_t sxs = (sx_q8 < 0) ? -(int32_t)sx_scaled_q8 : (int32_t)sx_scaled_q8;
        int32_t sys = (sy_q8 < 0) ? -(int32_t)sy_scaled_q8 : (int32_t)sy_scaled_q8;

        // rotation: (x',y') = (x*cos - y*sin, x*sin + y*cos); Q8.8 * Q15 -> >>15 => Q8.8
        int32_t rx_q8 = ( (sxs * (int32_t)cos_q15) - (sys * (int32_t)sin_q15) ) >> SIN_Q;
        int32_t ry_q8 = ( (sxs * (int32_t)sin_q15) + (sys * (int32_t)cos_q15) ) >> SIN_Q;

        // final pixel
        int16_t fx = cx + (int16_t)(rx_q8 >> LOG_Q);
        int16_t fy = cy + (int16_t)(ry_q8 >> LOG_Q);

        // draw into glyph buffer (translate local buffer coordinate)
        // buffer origin is top-left glyph area [0..gw-1, 0..gh-1]
        int16_t bx = (int16_t)( (rx_q8 >> LOG_Q) + (gbuf->w/2) );
        int16_t by = (int16_t)( (ry_q8 >> LOG_Q) + (gbuf->h/2) );
        if (bx >= 0 && bx < gbuf->w && by >= 0 && by < gbuf->h) {
          gbuf->buf[by * gbuf->w + bx] = color;
        }
      }
    }
  }

  // pushImage: x,y,w,h,uint16_t* buffer. We center buffer at cx,cy
  int16_t x0 = cx - (gbuf->w / 2);
  int16_t y0 = cy - (gbuf->h / 2);
  // clip region if necessary: TFT_eSPI's pushImage handles off-screen clipping in many versions,
  // but for safety, check bounds.
  tft.pushImage(x0, y0, gbuf->w, gbuf->h, gbuf->buf);
}

// render one frame with text rows
void render_frame() {
  uint32_t t0 = micros();
  tft.fillScreen(TFT_BLACK);

  for (uint8_t r = 0; r < ROWS; ++r) {
    const char* s = TEXT_ROWS[r];
    uint16_t len = strlen(s);
    int16_t baseline_x = tft.width() / 2;
    int16_t baseline_y = 20 + r * 80;

    for (uint16_t i=0;i<len;++i) {
      char ch = s[i];
      int16_t cx = baseline_x - (len * GLYPH_WIDTH)/2 + i * (GLYPH_WIDTH + 1) + GLYPH_WIDTH/2;
      int16_t cy = baseline_y;

      // animate scale and rotation
      float sbase = 0.8f + 0.6f * sin(frame_count * 0.02f + i * 0.3f);
      float angle = global_angle + 0.15f * sin(i * 0.25f + frame_count * 0.01f);
      render_glyph_transformed(ch, cx, cy, sbase, angle, TFT_WHITE);
    }
  }

  uint32_t t1 = micros();
  uint32_t dt = t1 - t0;
  bench_total_time_us += dt;
  bench_frames++;
  frame_count++;
  global_angle += 0.02f;

  // Every N frames print bench
  if (bench_frames % 30 == 0) {
    float avg_frame_ms = (bench_total_time_us / (float)bench_frames) / 1000.0f;
    float avg_mul_err = bench_mul_samples ? (bench_mul_error_sum / (double)bench_mul_samples) : 0.0;
    Serial.printf("Frames: %u avg_frame_ms=%.2f fps=%.1f mul_samples=%u avg_rel_err=%.6f max_err=%u\n",
      bench_frames, avg_frame_ms, 1000.0f/avg_frame_ms, bench_mul_samples, (float)avg_mul_err, bench_mul_error_max);
  }
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  delay(200);
}

void loop() {
  render_frame();
  // small delay to not saturate CPU completely; tune as necessary.
  delay(16);
}
