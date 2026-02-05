// compositor_optimized_fixed_phase_corrected.ino/.cpp
// Optimized tile compositor for ESP32 / AVR-friendly PROGMEM access.
// Implements requested optimizations and fixes PROGMEM table reads.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "arduino_tables.h" // user-provided tables header

// ------------------ Portable PROGMEM read helpers ------------------
#ifdef __AVR__
  #include <avr/pgmspace.h>
  static inline uint8_t read_u8(const uint8_t *addr) { return pgm_read_byte(addr); }
  static inline uint16_t read_u16(const uint16_t *addr) { return pgm_read_word(addr); }
  static inline int16_t read_s16(const int16_t *addr) { return (int16_t)pgm_read_word((const uint16_t*)addr); }
#else
  static inline uint8_t read_u8(const uint8_t *addr) { return *addr; }
  static inline uint16_t read_u16(const uint16_t *addr) { return *addr; }
  static inline int16_t read_s16(const int16_t *addr) { return *addr; }
#endif

// ------------------ Configuration (power-of-two tile size) ------------------
#define TILE_SHIFT 6                // 2^6 = 64 tile size
#define TILE_SIZE (1u << TILE_SHIFT)
#define MAX_OUTSTANDING_DMA 16
#define LOG_Q 8
#define SIN_Q 15
#define SIN_SIZE 512

TFT_eSPI tft = TFT_eSPI();

// Cached display dimensions (set in setup)
static uint16_t g_tft_w = 0;
static uint16_t g_tft_h = 0;

// ------------------ Phase accumulator (Q16) and helpers ------------------
static const uint32_t PHASE_Q = 16u;
static const uint32_t PHASE_ONE = (1u << PHASE_Q);

// ANIM_STEP chosen to match previous ~0.04f rad/frame behavior (converted to fraction of full cycle)
static const uint32_t ANIM_STEP_Q16 = (uint32_t)(0.04f / (2.0f * 3.14159265358979323846f) * (float)PHASE_ONE + 0.5f);

// Character offset constants converted to Q16 fractions of full cycle
static const uint32_t CHAR_SCALE_OFF_Q16  = (uint32_t)(0.5f  / (2.0f * 3.14159265358979323846f) * (float)PHASE_ONE + 0.5f);
static const uint32_t CHAR_ANGLE_OFF_Q16  = (uint32_t)(0.35f / (2.0f * 3.14159265358979323846f) * (float)PHASE_ONE + 0.5f);

// Convert Q16 phase -> SIN table index
static inline uint16_t phase_q16_to_sin_idx(uint32_t phase_q16) {
  return (uint16_t)(((uint32_t)phase_q16 * (uint32_t)SIN_SIZE) >> PHASE_Q) & (SIN_SIZE - 1);
}

// Global integer phase accumulator (Q16)
static uint32_t g_anim_phase_q16 = 0; // wraps automatically

// ------------------ Glyph lookup (O(1)) ------------------
static int8_t glyph_idx[256];

// ------------------ Tile-based compositor types ------------------

struct Tile {
  uint16_t x0, y0;
  uint16_t w, h;
  uint16_t *buf;                 // pixel buffer (RGB565)
  bool dirty;
  size_t capacity_n;             // number of pixels allocated
  bool buf_dma_allocated;        // true if allocated via heap_caps_*
  
  Tile(): x0(0), y0(0), w(0), h(0), buf(nullptr), dirty(false), capacity_n(0), buf_dma_allocated(false) {}

  void init(uint16_t _x0, uint16_t _y0, uint16_t _w, uint16_t _h) {
    x0 = _x0; y0 = _y0; w = _w; h = _h;
    size_t n = (size_t)w * (size_t)h;
    size_t bytes = n * sizeof(uint16_t);

    if (buf && capacity_n == n) {
      if (bytes) memset(buf, 0, bytes);
      dirty = false;
      return;
    }

    if (buf) {
      if (buf_dma_allocated) heap_caps_free(buf);
      else free(buf);
      buf = nullptr;
      capacity_n = 0;
      buf_dma_allocated = false;
    }

    // Prefer DMA-capable zeroed memory
    buf = (uint16_t*) heap_caps_calloc(n, sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
    if (buf) {
      buf_dma_allocated = true;
      capacity_n = n;
    } else {
      // fallback to normal calloc
      buf = (uint16_t*) calloc(n, sizeof(uint16_t));
      if (buf) {
        buf_dma_allocated = false;
        capacity_n = n;
      } else {
        capacity_n = 0;
      }
    }

    dirty = false;
  }

  inline IRAM_ATTR void clearTo(uint16_t color = 0x0000) {
    if (!buf) return;
    size_t n = (size_t)w * (size_t)h;
    if (color == 0) {
      memset(buf, 0, n * sizeof(uint16_t));
    } else {
      for (size_t i = 0; i < n; ++i) buf[i] = color;
    }
    dirty = false;
  }

  inline IRAM_ATTR void writePixelLocal(int16_t lx, int16_t ly, uint16_t color) {
    if (!buf) return;
    if (lx < 0 || ly < 0 || lx >= (int)w || ly >= (int)h) return;
    buf[(size_t)ly * (size_t)w + (size_t)lx] = color;
    dirty = true;
  }

  void freeBuf() {
    if (!buf) return;
    if (buf_dma_allocated) heap_caps_free(buf);
    else free(buf);
    buf = nullptr;
    capacity_n = 0;
    buf_dma_allocated = false;
  }
};

struct TileManager {
  uint16_t screen_w, screen_h;
  uint16_t tile_size;
  uint16_t cols, rows;
  Tile *tiles;

  TileManager(): screen_w(0), screen_h(0), tile_size(TILE_SIZE), cols(0), rows(0), tiles(nullptr) {}

  void init(uint16_t sw, uint16_t sh, uint16_t tsize = TILE_SIZE) {
    screen_w = sw; screen_h = sh; tile_size = tsize;
    cols = (screen_w + tile_size - 1) >> TILE_SHIFT;
    rows = (screen_h + tile_size - 1) >> TILE_SHIFT;
    size_t count = (size_t)cols * (size_t)rows;

    tiles = (Tile*) malloc(sizeof(Tile) * count);
    if (!tiles) {
      tiles = (Tile*) calloc(count, sizeof(Tile));
      if (!tiles) {
        Serial.println("ERROR: TileManager tile allocation failed!");
        return;
      }
    }

    for (uint16_t r = 0; r < rows; ++r) {
      for (uint16_t c = 0; c < cols; ++c) {
        uint16_t x0 = c * tile_size;
        uint16_t y0 = r * tile_size;
        uint16_t w = min((int)tile_size, (int)(screen_w - x0));
        uint16_t h = min((int)tile_size, (int)(screen_h - y0));
        Tile &t = tiles[(size_t)r * cols + c];
        new (&t) Tile();
        t.init(x0, y0, w, h);
      }
    }
  }

  inline IRAM_ATTR void writePixelGlobal(int16_t x, int16_t y, uint16_t color) {
    if ((uint32_t)x >= screen_w || (uint32_t)y >= screen_h) return;
    uint16_t tx = (uint16_t)x >> TILE_SHIFT;
    uint16_t ty = (uint16_t)y >> TILE_SHIFT;
    Tile &t = tiles[(size_t)ty * cols + tx];
    t.writePixelLocal(x - (int16_t)t.x0, y - (int16_t)t.y0, color);
  }

  void frameClear(uint16_t bgcolor = 0x0000) {
    size_t count = (size_t)cols * rows;
    for (size_t i = 0; i < count; ++i) tiles[i].clearTo(bgcolor);
  }

  void flush(TFT_eSPI &tft_ref) {
    uint16_t outstanding = 0;
    for (uint16_t rr = 0; rr < rows; ++rr) {
      uint16_t cc = 0;
      while (cc < cols) {
        uint16_t first = cc;
        while (first < cols && !tiles[(size_t)rr * cols + first].dirty) first++;
        if (first >= cols) break;
        uint16_t last = first;
        while (last + 1 < cols && tiles[(size_t)rr * cols + last + 1].dirty) last++;

        uint16_t seg_x0 = tiles[(size_t)rr * cols + first].x0;
        uint16_t seg_y0 = tiles[(size_t)rr * cols + first].y0;
        uint32_t total_w = 0;
        uint32_t seg_h = tiles[(size_t)rr * cols + first].h;
        for (uint16_t k = first; k <= last; ++k) total_w += tiles[(size_t)rr * cols + k].w;

        size_t seg_pixels = (size_t)total_w * (size_t)seg_h;
        size_t seg_bytes = seg_pixels * sizeof(uint16_t);

        uint16_t *tmp_buf = nullptr;
        bool tmp_dma = false;
        tmp_buf = (uint16_t*) heap_caps_malloc(seg_bytes, MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
        if (tmp_buf) tmp_dma = true;
        else {
          tmp_buf = (uint16_t*) malloc(seg_bytes);
          if (!tmp_buf) {
            for (uint16_t k = first; k <= last; ++k) {
              Tile &t = tiles[(size_t)rr * cols + k];
              if (t.buf) {
                tft_ref.pushImageDMA(t.x0, t.y0, t.w, t.h, t.buf);
                outstanding++;
                t.dirty = false;
                if (outstanding >= MAX_OUTSTANDING_DMA) {
                  while (tft_ref.dmaBusy()) { yield(); }
                  outstanding = 0;
                }
              }
            }
            cc = last + 1;
            continue;
          }
        }

        for (uint32_t row_offset = 0; row_offset < seg_h; ++row_offset) {
          uint16_t *dst = tmp_buf + (size_t)row_offset * (size_t)total_w;
          size_t dst_x = 0;
          for (uint16_t k = first; k <= last; ++k) {
            Tile &t = tiles[(size_t)rr * cols + k];
            uint16_t *src = t.buf + (size_t)row_offset * (size_t)t.w;
            memcpy(dst + dst_x, src, (size_t)t.w * sizeof(uint16_t));
            dst_x += t.w;
          }
        }

        tft_ref.pushImageDMA(seg_x0, seg_y0, (uint16_t)total_w, (uint16_t)seg_h, tmp_buf);
        outstanding++;

        for (uint16_t k = first; k <= last; ++k) {
          tiles[(size_t)rr * cols + k].dirty = false;
        }

        if (tmp_dma) heap_caps_free(tmp_buf);
        else free(tmp_buf);

        if (outstanding >= MAX_OUTSTANDING_DMA) {
          while (tft_ref.dmaBusy()) { yield(); }
          outstanding = 0;
        }

        cc = last + 1;
      }
    }

    while (tft_ref.dmaBusy()) { yield(); }
  }
};

TileManager gTiles;

// ------------------ Fast Math (IRAM_ATTR inline) ------------------

static inline IRAM_ATTR uint8_t fast_msb16(uint16_t v) {
  if (v >> 8) return 8 + read_u8(&msb_table[v >> 8]);
  return read_u8(&msb_table[v & 0xFF]);
}

static inline IRAM_ATTR int32_t fast_log2_q8_8(uint16_t v) {
  if (v == 0) return -32768;
  uint8_t e = fast_msb16(v);
  uint8_t mant8 = (uint8_t)(((uint32_t)v << (15 - e)) >> 8);
  return ((int32_t)(e - 7) << LOG_Q) + (int32_t)read_u16(&log2_table_q8[mant8]);
}

static inline IRAM_ATTR uint32_t fast_exp2_from_q8_8(int32_t log_q8_8) {
  if (log_q8_8 <= -2048) return 0;
  int32_t integer = log_q8_8 >> LOG_Q;
  uint8_t frac = (uint8_t)(log_q8_8 & 0xFF);
  uint16_t exp_frac = read_u16(&exp2_table_q8[frac]);
  if (integer >= 32) return 0xFFFFFFFFUL;
  if (integer >= 8)  return (uint32_t)exp_frac << (integer - 8);
  return (uint32_t)exp_frac >> (8 - integer);
}

static inline IRAM_ATTR uint32_t fast_log_mul_u16(uint16_t a, uint16_t b) {
  if (a == 0 || b == 0) return 0;
  return fast_exp2_from_q8_8(fast_log2_q8_8(a) + fast_log2_q8_8(b));
}

// -------------------- Rendering pipeline --------------------

void build_glyph_map() {
  for (int i = 0; i < 256; ++i) glyph_idx[i] = -1;
  uint16_t glyph_count = read_u16(&GLYPH_COUNT);
  for (int i = 0; i < (int)glyph_count; ++i) {
    char ch = (char)read_u8((const uint8_t*)&GLYPH_CHAR_LIST[i]);
    glyph_idx[(uint8_t)ch] = (int8_t)i;
  }
}

void draw_glyph_into_tiles(char ch, int16_t cx, int16_t cy, float scale_f, float angle_rad, uint16_t color) {
  int idx = glyph_idx[(uint8_t)ch];
  if (idx < 0) return;

  const uint8_t gw = (uint8_t)read_u8(&GLYPH_WIDTH);
  const uint8_t gh = (uint8_t)read_u8(&GLYPH_HEIGHT);

  uint16_t scale_q8 = (uint16_t)(scale_f * 256.0f);

  const uint16_t tft_h = gTiles.screen_h;
  int16_t py = constrain(cy, 0, (int)tft_h - 1);
  uint16_t persp_q8 = read_u16(&perspective_scale_table_q8[(py * 255) / tft_h]);
  uint32_t combined_scale_q8 = (fast_log_mul_u16(scale_q8, persp_q8) >> LOG_Q);

  // angle normalization + index into trig table
  float ang = fmodf(angle_rad, 2.0f * PI);
  if (ang < 0) ang += 2.0f * PI;
  uint16_t aidx = (uint16_t)((ang / (2.0f * PI)) * (float)SIN_SIZE) % SIN_SIZE;

  int16_t cos_q15 = read_s16(&cos_table_q15[aidx]);
  int16_t sin_q15 = read_s16(&sin_table_q15[aidx]);

  int16_t hw = gw >> 1;
  int16_t hh = gh >> 1;

  uint16_t glyph_width = gw; // small local copies for speed
  uint16_t glyph_height = gh;

  // GLYPH_BITMAPS likely stored as uint16_t; read with read_u16
  const uint16_t *glyph_base = &GLYPH_BITMAPS[(size_t)idx * glyph_width];

  for (uint8_t col = 0; col < glyph_width; ++col) {
    uint32_t colbyte = (uint32_t)read_u16(&glyph_base[col]);
    if (colbyte == 0) continue;
    for (uint8_t row = 0; row < glyph_height; ++row) {
      if (colbyte & (1u << row)) {
        int32_t sx_q8 = (int32_t)(col - hw) << LOG_Q;
        int32_t sy_q8 = (int32_t)(row - hh) << LOG_Q;

        int32_t sxs = (sx_q8 * (int32_t)combined_scale_q8) >> LOG_Q;
        int32_t sys = (sy_q8 * (int32_t)combined_scale_q8) >> LOG_Q;

        int32_t rx_q8 = ( (sxs * (int32_t)cos_q15) - (sys * (int32_t)sin_q15) ) >> SIN_Q;
        int32_t ry_q8 = ( (sxs * (int32_t)sin_q15) + (sys * (int32_t)cos_q15) ) >> SIN_Q;

        gTiles.writePixelGlobal(cx + (rx_q8 >> LOG_Q), cy + (ry_q8 >> LOG_Q), color);
      }
    }
  }
}

void draw_string_dynamic(const char* text, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                         float base_scale, int16_t spacing_x, int16_t spacing_y,
                         bool dynamic_scaling, uint16_t color) {
  if (!text || text[0] == '\0') return;

  int16_t box_w = x1 - x0;
  const char* line_ptr = text;
  int16_t current_y_offset = spacing_y / 2;
  int global_char_idx = 0;

  while (*line_ptr != '\0' && (y0 + current_y_offset) < y1) {
    int16_t char_count = 0;
    const char* peek = line_ptr;

    while (*peek != '\0' && *peek != '\n') {
      if (char_count > 0 && ((char_count + 1) * spacing_x) > box_w) break;
      char_count++;
      peek++;
    }

    if (char_count > 0) {
      int16_t row_width = (char_count - 1) * spacing_x;
      int16_t row_start_x = x0 + (box_w - row_width) / 2;
      int16_t row_y = y0 + current_y_offset;

      for (int i = 0; i < char_count; i++) {
        char c = line_ptr[i];
        if (c > 32) {
          int16_t cx = row_start_x + (i * spacing_x);
          float sbase = base_scale;
          float angle = 0.0f;

          if (dynamic_scaling) {
            // scale-phase: phase1 = g_anim_phase * 1.3 + (global_char_idx + i) * 0.5
            uint32_t phase_scaled_q16 = (uint32_t)(((uint64_t)g_anim_phase_q16 * 13u) / 10u);
            uint32_t char_offset_scale_q16 = (uint32_t)((global_char_idx + i) * (int)CHAR_SCALE_OFF_Q16);
            uint32_t phase1_q16 = phase_scaled_q16 + char_offset_scale_q16;
            uint16_t sidx = phase_q16_to_sin_idx(phase1_q16);
            int16_t sin_q15_val = read_s16(&sin_table_q15[sidx]);
            sbase += 0.4f * ((float)sin_q15_val / 32768.0f);

            // angle-phase: phase2 = g_anim_phase + (global_char_idx + i) * 0.35
            uint32_t char_offset_angle_q16 = (uint32_t)((global_char_idx + i) * (int)CHAR_ANGLE_OFF_Q16);
            uint32_t phase2_q16 = g_anim_phase_q16 + char_offset_angle_q16;
            uint16_t aidx = phase_q16_to_sin_idx(phase2_q16);
            int16_t cos_q15_val = read_s16(&cos_table_q15[aidx]);
            angle = 0.25f * ((float)cos_q15_val / 32768.0f);
          }

          draw_glyph_into_tiles(c, cx, row_y, sbase, angle, color);
        }
      }
      global_char_idx += char_count;
    }

    line_ptr += char_count;
    current_y_offset += spacing_y;
    if (*line_ptr == '\n') line_ptr++;
    else if (*line_ptr == ' ') line_ptr++;
  }
}

// -------------------- Main loop --------------------

const char* multi_line_text = "BOUNDING BOX\nSYSTEM\nSUPPORTS X/Y SPACING LINEWRAP \nAND RIGID SCALING";
char fps_buf[16];
uint32_t frame_count = 0;
uint32_t last_fps_time = 0;

void render_frame() {
    uint32_t start_micros = micros();

    // Update integer phase
    g_anim_phase_q16 += ANIM_STEP_Q16;
    if (g_anim_phase_q16 >= PHASE_ONE) g_anim_phase_q16 -= PHASE_ONE;

    gTiles.frameClear(0x0000);

    int16_t screen_w = (int16_t) gTiles.screen_w;
    draw_string_dynamic("COMPOSITOR V3", 5, 10, screen_w, 50, 2.8f, 20, 20, true, TFT_CYAN);
    draw_string_dynamic(multi_line_text, 40, 60, 280, 180, 2.8f, 14, 16, true, TFT_WHITE);

    if (frame_count % 30 == 0) {
        uint32_t now = millis();
        uint32_t dt = now - last_fps_time;
        if (dt == 0) dt = 1;
        float fps = (30.0f * 1000.0f) / (float)dt;
        snprintf(fps_buf, sizeof(fps_buf), "FPS:%.1f", fps);
        last_fps_time = now;
    }
    draw_string_dynamic(fps_buf, 0, 190, screen_w, 240, 8.0f, 20, 10, true, TFT_GREEN);

    gTiles.flush(tft);
    frame_count++;
    (void)start_micros;
}

void setup() {
    Serial.begin(115200);
    tft.init();
    tft.initDMA();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    g_tft_w = tft.width();
    g_tft_h = tft.height();

    gTiles.init(g_tft_w, g_tft_h, TILE_SIZE);

    build_glyph_map();

    last_fps_time = millis();
}

void loop() {
    tft.startWrite();
    render_frame();
    tft.endWrite();
    yield();
}
