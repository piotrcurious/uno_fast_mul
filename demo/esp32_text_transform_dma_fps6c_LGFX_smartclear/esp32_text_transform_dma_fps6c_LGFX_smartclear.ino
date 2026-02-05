#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>
#include "arduino_tables.h" 

#define SWAP_RGB(c) (((c) << 8) | ((c) >> 8))

// ------------------ Hardware Configuration ------------------
class LGFX_ESP32 : public lgfx::LGFX_Device {
    lgfx::Panel_Device* _panel_instance;
    lgfx::Bus_SPI* _bus_instance;
public:
    LGFX_ESP32() {
        auto bus = new lgfx::Bus_SPI();
        auto panel = new lgfx::Panel_ST7789();
        {
            auto cfg = bus->config();
            cfg.spi_host = VSPI_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 80000000;
            cfg.freq_read  = 16000000;
            cfg.pin_sclk = 18;
            cfg.pin_mosi = 23;
            cfg.pin_miso = -1;
            cfg.pin_dc   = 16;
            bus->config(cfg);
        }
        _bus_instance = bus;
        panel->setBus(bus);
        {
            auto cfg = panel->config();
            cfg.pin_cs           = 5;
            cfg.pin_rst          = 17;
            cfg.pin_busy         = -1;
            cfg.panel_width      = 240;
            cfg.panel_height     = 320;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable         = true;
            cfg.invert           = true;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = true;
            panel->config(cfg);
        }
        _panel_instance = panel;
        setPanel(panel);
    }
};

LGFX_ESP32 tft;

// ------------------ Configuration ------------------
#define TILE_SIZE 16         // Increased size slightly for better DMA efficiency
#define LOG_Q 8
#define SIN_Q 15
#define SIN_SIZE 512

float g_anim_phase = 0.0f;

// ------------------ Tile-based Compositor ------------------

struct Tile {
  uint16_t x0, y0;   
  uint16_t w, h;     
  uint16_t *buf;     
  bool dirty_curr;   // Changed in current frame
  bool dirty_prev;   // Changed in previous frame
  
  Tile(): x0(0), y0(0), w(0), h(0), buf(nullptr), dirty_curr(false), dirty_prev(false) {}
  
  void init(uint16_t _x0, uint16_t _y0, uint16_t _w, uint16_t _h) {
    x0 = _x0; y0 = _y0; w = _w; h = _h;
    size_t n = (size_t)w * (size_t)h;
    if (buf) { free(buf); buf = nullptr; }
    
    buf = (uint16_t*) heap_caps_malloc(n * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
    if (!buf) buf = (uint16_t*) malloc(n * sizeof(uint16_t)); 
    
    dirty_curr = false;
    dirty_prev = false;
    if (buf) memset(buf, 0, n * sizeof(uint16_t));
  }
  
  // Only clears the buffer if it was dirty in previous frame.
  // Resets dirty_curr to track new writes this frame.
  void prepareFrame(uint16_t bgcolor) {
    if (dirty_prev || dirty_curr) {
      size_t n = (size_t)w * (size_t)h;
      if (bgcolor == 0) memset(buf, 0, n * sizeof(uint16_t));
      else {
        for (size_t i=0; i<n; ++i) buf[i] = bgcolor;
      }
    }
    // Shift states
    dirty_prev = dirty_curr;
    dirty_curr = false;
  }

  inline void writePixelLocal(int16_t lx, int16_t ly, uint16_t color) {
    if (!buf || lx < 0 || ly < 0 || lx >= (int)w || ly >= (int)h) return;
    buf[ly * w + lx] = color;
    dirty_curr = true;
  }
};

struct TileManager {
  uint16_t screen_w, screen_h;
  uint16_t tile_size;
  uint16_t cols, rows;
  Tile *tiles;

  TileManager(): tiles(nullptr) {}

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
        uint16_t w = min((int)tile_size, (int)(screen_w - x0));
        uint16_t h = min((int)tile_size, (int)(screen_h - y0));
        Tile &t = tiles[r*cols + c];
        new (&t) Tile();
        t.init(x0, y0, w, h);
      }
    }
  }

  inline void writePixelGlobal(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || y < 0 || x >= (int)screen_w || y >= (int)screen_h) return;
    uint16_t tx = x / tile_size;
    uint16_t ty = y / tile_size;
    tiles[ty * cols + tx].writePixelLocal(x - (tx * tile_size), y - (ty * tile_size), color);
  }

  void startFrame(uint16_t bgcolor = 0x0000) {
    uint32_t count = (uint32_t)cols * rows;
    for (uint32_t i=0; i<count; ++i) tiles[i].prepareFrame(bgcolor);
  }

  void flush(LGFX_ESP32 &tft_ref) {
    uint32_t count = (uint32_t)cols * rows;
    for (uint32_t i=0; i < count; ++i) {
      Tile &t = tiles[i];
      // Draw if it is dirty NOW, or if it WAS dirty and now needs to be cleared 
      // (because prepareFrame already wiped the buffer for us)
      if (t.dirty_curr || t.dirty_prev) {
        tft_ref.pushImage(t.x0, t.y0, t.w, t.h, t.buf);
      }
    }
  }
};

TileManager gTiles;

// ------------------ Fast Math ------------------

static inline uint8_t fast_msb16(uint16_t v) {
  if (v >> 8) return 8 + msb_table[v >> 8];
  return msb_table[v & 0xFF];
}

static inline int32_t fast_log2_q8_8(uint16_t v) {
  if (v == 0) return -32768; 
  uint8_t e = fast_msb16(v);
  uint8_t mant8 = (uint8_t)(((uint32_t)v << (15 - e)) >> 8);
  return ((int32_t)(e - 7) << LOG_Q) + (int32_t)log2_table_q8[mant8];
}

static inline uint32_t fast_exp2_from_q8_8(int32_t log_q8_8) {
  if (log_q8_8 <= -2048) return 0;
  int32_t integer = log_q8_8 >> LOG_Q;
  uint8_t frac = (uint8_t)(log_q8_8 & 0xFF);
  uint16_t exp_frac = exp2_table_q8[frac];
  if (integer >= 32) return 0xFFFFFFFFUL;
  if (integer >= 8)  return (uint32_t)exp_frac << (integer - 8);
  return (uint32_t)exp_frac >> (8 - integer);
}

static inline uint32_t fast_log_mul_u16(uint16_t a, uint16_t b) {
  if (a == 0 || b == 0) return 0;
  return fast_exp2_from_q8_8(fast_log2_q8_8(a) + fast_log2_q8_8(b));
}

// -------------------- Rendering Pipeline --------------------

void draw_glyph_into_tiles(char ch, int16_t cx, int16_t cy, float scale_f, float angle_rad, uint16_t color) {
  int idx = -1;
  for (uint16_t i = 0; i < GLYPH_COUNT; ++i) {
    if (GLYPH_CHAR_LIST[i] == ch) { idx = i; break; }
  }
  if (idx < 0) return;

  uint16_t scale_q8 = (uint16_t)(scale_f * 256.0f);
  int16_t py = constrain(cy, 0, (int)tft.height() - 1);
  uint16_t persp_q8 = perspective_scale_table_q8[(py * 255) / tft.height()];
  uint32_t combined_scale_q8 = (fast_log_mul_u16(scale_q8, persp_q8) >> LOG_Q);

  float ang = fmodf(angle_rad, 2.0f * PI);
  if (ang < 0) ang += 2.0f * PI;
  uint16_t aidx = (uint16_t)((ang / (2.0f * PI)) * SIN_SIZE) % SIN_SIZE;
  
  int16_t cos_q15 = cos_table_q15[aidx];
  int16_t sin_q15 = sin_table_q15[aidx];

  int16_t hw = GLYPH_WIDTH >> 1;
  int16_t hh = GLYPH_HEIGHT >> 1;

  for (uint8_t col = 0; col < GLYPH_WIDTH; ++col) {
    uint32_t colbyte = GLYPH_BITMAPS[idx * GLYPH_WIDTH + col];
    if (colbyte == 0) continue;
    for (uint8_t row = 0; row < GLYPH_HEIGHT; ++row) {
      if (colbyte & (1 << row)) {
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
            sbase += 0.4f * sinf(g_anim_phase * 1.3f + (global_char_idx + i) * 0.5f);
            angle = 0.25f * cosf((global_char_idx + i) * 0.35f + g_anim_phase);
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

// -------------------- Main Loop --------------------

const char* multi_line_text = "DIRTY RECT\nSYSTEM ACTIVE\nONLY UPDATING\nMOVED PIXELS";
char fps_buf[16] = "FPS: 0.0";
uint32_t frame_count = 0;
uint32_t last_fps_time = 0;

void render_frame() {
    g_anim_phase += 0.04f;
    if (g_anim_phase > 62.83f) g_anim_phase -= 62.83f; 

    // 1. Shift dirty flags and clear previous pixels in buffer
    gTiles.startFrame(0x0000);

    // 2. Draw new content (sets dirty_curr)
    draw_string_dynamic("OPTIMIZED V4", 5, 10, tft.width(), 50, 2.8f, 20, 20, true, SWAP_RGB(TFT_CYAN));
    draw_string_dynamic(multi_line_text, 40, 60, 280, 180, 2.8f, 14, 16, true, 0xFFFF);
    
    if (frame_count % 30 == 0) {
        uint32_t now = millis();
        float fps = 30000.0f / (float)(now - last_fps_time);
        sprintf(fps_buf, "FPS:%.1f", fps);
        last_fps_time = now;
    }
    draw_string_dynamic(fps_buf, 0, 190, tft.width(), 240, 8.0f, 20, 10, true, SWAP_RGB(TFT_GREEN));

    // 3. Flush only what changed (newly dirty OR previously dirty)
    gTiles.flush(tft);
    frame_count++;
}

void setup() {
    Serial.begin(115200);
    tft.init();
    tft.initDMA(); 
    tft.setRotation(1);
    tft.fillScreen(0x0000);
    
    gTiles.init(tft.width(), tft.height(), TILE_SIZE);
    last_fps_time = millis();
}

void loop() {
    tft.startWrite();
    render_frame();
    tft.endWrite();
    yield();
}
