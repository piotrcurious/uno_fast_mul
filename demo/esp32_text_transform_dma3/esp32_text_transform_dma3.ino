// ESP32 demo: Tile-based compositor for DMA-safe glyph rendering
// Integrates fast log-based multiplication, perspective tables, and grid-based compositing.

#include <Arduino.h>
#include <TFT_eSPI.h> // https://github.com/Bodmer/TFT_eSPI
#include <esp_heap_caps.h>
#include "arduino_tables.h" 

// ------------------ Configuration ------------------
#define TILE_SIZE 64           // Grid size (32, 64, or 128)
#define MAX_OUTSTANDING_DMA 4  // Number of parallel DMA transactions
#define LOG_Q 8
#define SIN_Q 15
#define SIN_SIZE 512

TFT_eSPI tft = TFT_eSPI();

// ------------------ Tile-based Compositor ------------------

struct Tile {
  uint16_t x0, y0;   
  uint16_t w, h;     
  uint16_t *buf;     // DMA-capable pixel buffer (RGB565)
  bool dirty;
  
  Tile(): x0(0), y0(0), w(0), h(0), buf(nullptr), dirty(false) {}
  
  void init(uint16_t _x0, uint16_t _y0, uint16_t _w, uint16_t _h) {
    x0 = _x0; y0 = _y0; w = _w; h = _h;
    size_t n = (size_t)w * (size_t)h;
    if (buf) free(buf);
    // Attempt to allocate in DMA capable memory
    buf = (uint16_t*) heap_caps_malloc(n * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!buf) buf = (uint16_t*) malloc(n * sizeof(uint16_t)); // Fallback
    dirty = false;
    if (buf) memset(buf, 0, n * sizeof(uint16_t));
  }
  
  void clearTo(uint16_t color = 0x0000) {
    if (!buf) return;
    size_t n = (size_t)w * (size_t)h;
    for (size_t i=0; i<n; ++i) buf[i] = color;
    dirty = false;
  }

  inline void writePixelLocal(int16_t lx, int16_t ly, uint16_t color) {
    if (!buf || lx < 0 || ly < 0 || lx >= (int)w || ly >= (int)h) return;
    buf[ly * w + lx] = color;
    dirty = true;
  }

  void freeBuf() {
    if (buf) { free(buf); buf = nullptr; }
  }
};

struct TileManager {
  uint16_t screen_w, screen_h;
  uint16_t tile_size;
  uint16_t cols, rows;
  Tile *tiles;

  TileManager(): screen_w(0), screen_h(0), tile_size(TILE_SIZE), cols(0), rows(0), tiles(nullptr) {}

  void init(uint16_t sw, uint16_t sh, uint16_t tsize=TILE_SIZE) {
    screen_w = sw; screen_h = sh; tile_size = tsize;
    cols = (screen_w + tile_size - 1) / tile_size;
    rows = (screen_h + tile_size - 1) / tile_size;
    size_t count = (size_t)cols * rows;
    tiles = (Tile*)malloc(sizeof(Tile) * count);
    
    for (uint16_t r=0; r<rows; ++r) {
      for (uint16_t c=0; c<cols; ++c) {
        uint16_t x0 = c * tile_size;
        uint16_t y0 = r * tile_size;
        uint16_t w = min(tile_size, (uint16_t)(screen_w - x0));
        uint16_t h = min(tile_size, (uint16_t)(screen_h - y0));
        Tile &t = tiles[r*cols + c];
        new (&t) Tile();
        t.init(x0, y0, w, h);
      }
    }
  }

  inline Tile &tileAtIdx(uint16_t tx, uint16_t ty) { return tiles[ty * cols + tx]; }

  inline bool coordToTile(int16_t x, int16_t y, uint16_t &tx, uint16_t &ty) {
    if (x < 0 || y < 0 || x >= (int)screen_w || y >= (int)screen_h) return false;
    tx = x / tile_size;
    ty = y / tile_size;
    return true;
  }

  void frameClear(uint16_t bgcolor = 0x0000) {
    uint32_t count = (uint32_t)cols * rows;
    for (uint32_t i=0; i<count; ++i) {
      tiles[i].clearTo(bgcolor);
    }
  }

  inline void writePixelGlobal(int16_t x, int16_t y, uint16_t color) {
    uint16_t tx, ty;
    if (!coordToTile(x, y, tx, ty)) return;
    Tile &t = tileAtIdx(tx, ty);
    t.writePixelLocal(x - t.x0, y - t.y0, color);
  }

  void flush(TFT_eSPI &tft) {
    uint16_t outstanding = 0;
    uint32_t count = (uint32_t)cols * rows;
    for (uint32_t i=0; i<count; ++i) {
      Tile &t = tiles[i];
      if (!t.dirty) continue;
      
      tft.pushImageDMA(t.x0, t.y0, t.w, t.h, t.buf);
      outstanding++;
      
      if (outstanding >= MAX_OUTSTANDING_DMA) {
        while (tft.dmaBusy()) { yield(); }
        outstanding = 0;
      }
      t.dirty = false;
    }
    while (tft.dmaBusy()) { yield(); }
  }

  void deinit() {
    if (!tiles) return;
    size_t count = (size_t)cols * rows;
    for (size_t i=0; i<count; ++i) tiles[i].freeBuf();
    free(tiles); tiles = nullptr;
  }
};

// ------------------ Globals & Helpers ------------------

TileManager gTiles;

static inline uint8_t fast_msb16(uint16_t v) {
  if (v >> 8) return 8 + msb_table[v >> 8];
  return msb_table[v & 0xFF];
}

static inline void normalize_to_mant8(uint16_t v, uint8_t &mant8, int8_t &e_out) {
  if (v == 0) { mant8 = 0; e_out = -127; return; }
  uint8_t e = fast_msb16(v);
  mant8 = (uint8_t)(((uint32_t)v << (15 - e)) >> 8);
  e_out = (int8_t)e;
}

static inline int32_t fast_log2_q8_8(uint16_t v) {
  if (v == 0) return INT32_MIN;
  uint8_t mant8; int8_t e;
  normalize_to_mant8(v, mant8, e);
  return ((int32_t)(e - 7) << LOG_Q) + (int32_t)log2_table_q8[mant8];
}

static inline uint32_t fast_exp2_from_q8_8(int32_t log_q8_8) {
  if (log_q8_8 <= -32768) return 0;
  int32_t integer = log_q8_8 >> LOG_Q;
  uint8_t frac = (uint8_t)(log_q8_8 & 0xFF);
  uint16_t exp_frac = exp2_table_q8[frac];
  if (integer >= 32) return 0xFFFFFFFFUL;
  if (integer >= 8)  return (uint32_t)exp_frac << (integer - 8);
  if (integer >= 0)  return (uint32_t)exp_frac >> (8 - integer);
  int shift = -integer;
  return (shift >= 24) ? 0 : ((uint32_t)exp_frac) >> (8 + shift);
}

static inline uint32_t fast_log_mul_u16(uint16_t a, uint16_t b) {
  if (a == 0 || b == 0) return 0;
  return fast_exp2_from_q8_8(fast_log2_q8_8(a) + fast_log2_q8_8(b));
}

static inline uint16_t angle_to_index(float angle) {
  while (angle < 0) angle += 2.0f * PI;
  while (angle >= 2.0f * PI) angle -= 2.0f * PI;
  return (uint16_t)((angle / (2.0f * PI)) * (float)SIN_SIZE);
}

static inline int glyph_index_for_char(char c) {
  for (uint16_t i = 0; i < GLYPH_COUNT; ++i) {
    if (GLYPH_CHAR_LIST[i] == c) return i;
  }
  return -1;
}

// -------------------- Benchmarking --------------------
uint64_t bench_total_time_us = 0;
uint32_t bench_frames = 0;
uint32_t bench_mul_samples = 0;
double bench_mul_error_sum = 0.0;
uint32_t bench_mul_error_max = 0;

static inline void bench_record_mul(uint16_t a, uint16_t b) {
  uint32_t exact = (uint32_t)a * (uint32_t)b;
  uint32_t approx = fast_log_mul_u16(a, b);
  int32_t diff = (int32_t)approx - (int32_t)exact;
  uint32_t err = (diff >= 0) ? diff : -diff;
  bench_mul_samples++;
  bench_mul_error_sum += (double)err / (double)max((uint32_t)1, exact);
  if (err > bench_mul_error_max) bench_mul_error_max = err;
}

// -------------------- Rendering Pipeline --------------------

void draw_glyph_into_tiles(char ch, int16_t cx, int16_t cy, float scale_f, float angle_rad, uint16_t color) {
  int idx = glyph_index_for_char(ch);
  if (idx < 0) return;
  const uint8_t gw = GLYPH_WIDTH;
  const uint8_t gh = GLYPH_HEIGHT;

  uint16_t scale_q8 = (uint16_t)round(scale_f * (1 << LOG_Q));
  uint16_t persp_idx = (uint16_t)((cy / (float)tft.height()) * 255.0f);
  if (persp_idx > 255) persp_idx = 255;
  uint16_t persp_q8 = perspective_scale_table_q8[persp_idx];
  
  uint32_t combined_scale_q8 = (fast_log_mul_u16(scale_q8, persp_q8) >> LOG_Q);
  bench_record_mul(scale_q8, persp_q8);

  uint16_t aidx = angle_to_index(angle_rad);
  int16_t cos_q15 = cos_table_q15[aidx % SIN_SIZE];
  int16_t sin_q15 = sin_table_q15[aidx % SIN_SIZE];

  for (uint8_t col = 0; col < gw; ++col) {
    uint32_t colbyte = GLYPH_BITMAPS[idx * gw + col];
    for (uint8_t row = 0; row < gh; ++row) {
      if (colbyte & (1 << row)) {
        int16_t sx = (int16_t)col - (gw / 2);
        int16_t sy = (int16_t)row - (gh / 2);

        int32_t sx_q8 = ((int32_t)sx) << LOG_Q;
        int32_t sy_q8 = ((int32_t)sy) << LOG_Q;
        
        uint32_t asx = (uint32_t)(sx_q8 < 0 ? -sx_q8 : sx_q8);
        uint32_t asy = (uint32_t)(sy_q8 < 0 ? -sy_q8 : sy_q8);
        
        uint32_t sx_scaled_q8 = fast_log_mul_u16((uint16_t)min(asx, 65535UL), (uint16_t)combined_scale_q8) >> LOG_Q;
        uint32_t sy_scaled_q8 = fast_log_mul_u16((uint16_t)min(asy, 65535UL), (uint16_t)combined_scale_q8) >> LOG_Q;
        
        int32_t sxs = (sx_q8 < 0) ? -(int32_t)sx_scaled_q8 : (int32_t)sx_scaled_q8;
        int32_t sys = (sy_q8 < 0) ? -(int32_t)sy_scaled_q8 : (int32_t)sy_scaled_q8;

        int32_t rx_q8 = ( (sxs * (int32_t)cos_q15) - (sys * (int32_t)sin_q15) ) >> SIN_Q;
        int32_t ry_q8 = ( (sxs * (int32_t)sin_q15) + (sys * (int32_t)cos_q15) ) >> SIN_Q;

        int16_t fx = cx + (int16_t)(rx_q8 >> LOG_Q);
        int16_t fy = cy + (int16_t)(ry_q8 >> LOG_Q);

        gTiles.writePixelGlobal(fx, fy, color);
      }
    }
  }
}

const char* TEXT_ROWS[] = {
  "HELLO ESP32 DMA",
  "TILE COMPOSITOR",
  "64x64 GRID"
};
const uint8_t ROWS = sizeof(TEXT_ROWS) / sizeof(TEXT_ROWS[0]);
float global_angle = 0.0f;
uint32_t frame_count = 0;

void render_frame_tiled() {
  uint32_t t0 = micros();

  gTiles.frameClear(0x0000);

  for (uint8_t r = 0; r < ROWS; ++r) {
    const char* s = TEXT_ROWS[r];
    uint16_t len = strlen(s);
    int16_t baseline_x = tft.width() / 2;
    int16_t baseline_y = 40 + r * 60;

    for (uint16_t i=0; i<len; ++i) {
      int16_t cx = baseline_x - (len * GLYPH_WIDTH)/2 + i * (GLYPH_WIDTH + 1) + GLYPH_WIDTH/2;
      int16_t cy = baseline_y;

      float sbase = 2.8f + 0.5f * sin(frame_count * 0.04f + i * 0.3f);
      float angle = global_angle + 0.2f * sin(i * 0.25f + frame_count * 0.02f);

      draw_glyph_into_tiles(s[i], cx, cy, sbase, angle, TFT_WHITE);
    }
  }

  gTiles.flush(tft);

  uint32_t t1 = micros();
  bench_total_time_us += (t1 - t0);
  bench_frames++;
  frame_count++;
  global_angle += 0.02f;

  if (bench_frames % 30 == 0) {
    float avg_frame_ms = (bench_total_time_us / (float)bench_frames) / 1000.0f;
    Serial.printf("Frames: %u | %.2f ms (%.1f fps) | MulErr: %.6f\n", 
                  bench_frames, avg_frame_ms, 1000.0f/avg_frame_ms, 
                  (float)(bench_mul_samples ? bench_mul_error_sum/bench_mul_samples : 0));
  }
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.initDMA();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  
  gTiles.init(tft.width(), tft.height(), TILE_SIZE);
  delay(500);
}

void loop() {
  // startWrite/endWrite are handled internally or at the SPI level by pushImageDMA
  tft.startWrite();
  render_frame_tiled();
  tft.endWrite();
   delay(1); // Optional throttle
}
