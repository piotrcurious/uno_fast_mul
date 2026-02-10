#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>

// include generated tables & glyphs
#include "arduino_tables.h"
#include "glyph_paths.h"

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
#define TILE_SIZE 8         // minimum size is 8 (32bit align)
#define LOG_Q 8
#define SIN_Q 15
#define SIN_SIZE 512

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

  void prepareFrame(uint16_t bgcolor) {
    if (dirty_prev || dirty_curr) {
      size_t n = (size_t)w * (size_t)h;
      if (bgcolor == 0) memset(buf, 0, n * sizeof(uint16_t));
      else {
        for (size_t i=0; i<n; ++i) buf[i] = bgcolor;
      }
    }
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

// -------------------- Rendering Pipeline --------------------

void draw_glyph_into_tiles(TileManager &tiles, char ch, int16_t cx, int16_t cy, 
                           float scale_f, float angle_rad, uint16_t color) {
    int idx = -1;
    for (uint16_t i = 0; i < GLYPH_COUNT; ++i) {
        if (GLYPH_CHAR_LIST[i] == ch) { idx = i; break; }
    }
    if (idx < 0) return;

    const uint8_t gw = GLYPH_WIDTH;
    const uint8_t gh = GLYPH_HEIGHT;
    uint16_t scale_q8 = (uint16_t)(scale_f * (1 << LOG_Q));

    float sA = sinf(angle_rad);
    float cA = cosf(angle_rad);
    int16_t cos_q15 = (int16_t)(cA * 32767);
    int16_t sin_q15 = (int16_t)(sA * 32767);

    for (uint8_t col = 0; col < gw; ++col) {
        uint32_t colbyte = GLYPH_BITMAPS[idx * gw + col];
        if (!colbyte) continue;
        for (uint8_t row = 0; row < gh; ++row) {
            if (colbyte & (1 << row)) {
                int16_t sx = (int16_t)col - (gw / 2);
                int16_t sy = (int16_t)row - (gh / 2);

                int32_t sx_q8 = ((int32_t)sx) << LOG_Q;
                int32_t sy_q8 = ((int32_t)sy) << LOG_Q;
                
                uint32_t asx = abs(sx_q8);
                uint32_t asy = abs(sy_q8);
                uint32_t sx_scaled_q8 = fast_log_mul_u16((uint16_t)min(asx, 65535UL), scale_q8) >> LOG_Q;
                uint32_t sy_scaled_q8 = fast_log_mul_u16((uint16_t)min(asy, 65535UL), scale_q8) >> LOG_Q;
                
                int32_t sxs = (sx_q8 < 0) ? -(int32_t)sx_scaled_q8 : (int32_t)sx_scaled_q8;
                int32_t sys = (sy_q8 < 0) ? -(int32_t)sy_scaled_q8 : (int32_t)sy_scaled_q8;

                int32_t rx_q8 = ( (sxs * (int32_t)cos_q15) - (sys * (int32_t)sin_q15) ) >> 15;
                int32_t ry_q8 = ( (sxs * (int32_t)sin_q15) + (sys * (int32_t)cos_q15) ) >> 15;

                int16_t fx = cx + (int16_t)(rx_q8 >> LOG_Q);
                int16_t fy = cy + (int16_t)(ry_q8 >> LOG_Q);

                tiles.writePixelGlobal(fx, fy, color);
            }
        }
    }
}

// Helper to get character position from flat PROGMEM array
static inline PathPoint getVerseCharPos(uint8_t verseIdx, uint8_t charIdx) {
    uint16_t offset;
    memcpy_P(&offset, &VERSE_OFFSETS[verseIdx], 2);
    PathPoint p;
    memcpy_P(&p, &ALL_VERSE_CHARS[offset + charIdx], sizeof(PathPoint));
    return p;
}

static inline uint8_t getVerseLen(uint8_t verseIdx) {
    uint8_t len;
    memcpy_P(&len, &VERSE_LENGTHS[verseIdx], 1);
    return len;
}

uint16_t getVerseColor(uint8_t idx) {
    static const uint16_t colors[] = {0x3BFF, 0xF81F, 0x07FF, 0xFD20, 0x07E0, 0xFFE0};
    return colors[idx % 6];
}

// -------------------- Scene State --------------------

enum Phase { 
    FLYBY,        // Camera flies by each verse
    ZOOM_OUT,     // Zoom out to reveal full message
    FINAL         // Show complete message
};

Phase currentPhase = FLYBY;
uint8_t currentVerse = 0;
uint32_t phaseStartMs = 0;
const uint32_t FLYBY_DURATION = 1800;   // 1.8s per verse
const uint32_t ZOOM_DURATION = 5000;    // 5s zoom out

// Camera position
float cameraX = 0, cameraY = 0, cameraZoom = 1.0f, cameraAngle = 0.0f;

// -------------------- Rendering Functions --------------------

void drawVerseCurved(TileManager &tiles, uint8_t verseIdx, uint16_t color,
                        float camX, float camY, float camZoom, float camAngle) {
    uint8_t len = getVerseLen(verseIdx);
    char buf[128];
    strcpy_P(buf, (char*)pgm_read_ptr(&(VERSES[verseIdx])));
    const char* str = buf;

    for (int i = 0; i < len; i++) {
        PathPoint p = getVerseCharPos(verseIdx, i);

        float dx = p.x - camX;
        float dy = p.y - camY;
        
        // Rotate world around camera
        float rx = dx * cosf(-camAngle) - dy * sinf(-camAngle);
        float ry = dx * sinf(-camAngle) + dy * cosf(-camAngle);

        int16_t screenX = (int16_t)(rx * camZoom) + tft.width() / 2;
        int16_t screenY = (int16_t)(ry * camZoom) + tft.height() / 2;

        // Draw character rotated relative to camera
        draw_glyph_into_tiles(tiles, str[i], screenX, screenY, p.scale * camZoom, p.angle - camAngle, color);
    }
}

// Debug path rendering
void drawDebugPath(TileManager &tiles, float camX, float camY, float camZoom, float camAngle) {
    for (int i=0; i<NUM_MASTER_SEGMENTS; ++i) {
        Segment s;
        memcpy_P(&s, &MASTER_PATH[i], sizeof(Segment));

        auto transformX = [&](float x, float y) {
            float dx = x - camX; float dy = y - camY;
            return (int16_t)((dx * cosf(-camAngle) - dy * sinf(-camAngle)) * camZoom) + tft.width() / 2;
        };
        auto transformY = [&](float x, float y) {
            float dx = x - camX; float dy = y - camY;
            return (int16_t)((dx * sinf(-camAngle) + dy * cosf(-camAngle)) * camZoom) + tft.height() / 2;
        };

        int16_t x1 = transformX(s.x1, s.y1);
        int16_t y1 = transformY(s.x1, s.y1);
        int16_t x2 = transformX(s.x2, s.y2);
        int16_t y2 = transformY(s.x2, s.y2);

        // Simple line drawing into tiles (just points for now for speed/simplicity)
        float dx = x2 - x1;
        float dy = y2 - y1;
        float len = sqrt(dx*dx + dy*dy);
        if (len < 1) continue;
        for (float t=0; t<=1.0f; t += 1.0f/len) {
            tiles.writePixelGlobal(x1 + dx*t, y1 + dy*t, 0x03E0); // Dim green
        }
    }
}

void renderFlyby(uint8_t verseIdx, float progress) {
    uint8_t len = getVerseLen(verseIdx);
    
    // Map progress to character index
    float charProgress = progress * (len - 1);
    uint8_t c1 = (uint8_t)charProgress;
    uint8_t c2 = min((int)c1 + 1, (int)len - 1);
    float t = charProgress - c1;

    PathPoint p1 = getVerseCharPos(verseIdx, c1);
    PathPoint p2 = getVerseCharPos(verseIdx, c2);
    
    // Camera follows characters
    cameraX = p1.x + (p2.x - p1.x) * t;
    cameraY = p1.y + (p2.y - p1.y) * t;
    cameraAngle = p1.angle + (p2.angle - p1.angle) * t;
    cameraZoom = 4.0f;

    drawDebugPath(gTiles, cameraX, cameraY, cameraZoom, cameraAngle);

    // Render all verses for context
    for (uint8_t i = 0; i < NUM_VERSES; i++) {
        uint16_t color = getVerseColor(i);
        if (i != verseIdx) {
            // Dim non-active verses
            uint16_t r = ((color >> 11) & 0x1F) >> 1;
            uint16_t g = ((color >> 5) & 0x3F) >> 1;
            uint16_t b = (color & 0x1F) >> 1;
            color = (r << 11) | (g << 5) | b;
        }
        drawVerseCurved(gTiles, i, color, cameraX, cameraY, cameraZoom, cameraAngle);
    }
}

void renderZoomOut(float progress) {
    float startZoom = 4.0f;
    float endZoom = 0.2f;
    cameraZoom = startZoom + (endZoom - startZoom) * progress;
    
    cameraX = 600 * (1.0f - progress);
    cameraY = 0;
    cameraAngle *= 0.9f; // Straighten camera
    
    drawDebugPath(gTiles, cameraX, cameraY, cameraZoom, cameraAngle);

    for (uint8_t i = 0; i < NUM_VERSES; i++) {
        drawVerseCurved(gTiles, i, getVerseColor(i), cameraX, cameraY, cameraZoom, cameraAngle);
    }
}

void renderFinal() {
    cameraX = 600;
    cameraY = 0;
    cameraZoom = 0.2f;
    cameraAngle = 0;
    
    drawDebugPath(gTiles, cameraX, cameraY, cameraZoom, cameraAngle);

    for (uint8_t i = 0; i < NUM_VERSES; i++) {
        drawVerseCurved(gTiles, i, getVerseColor(i), cameraX, cameraY, cameraZoom, cameraAngle);
    }
    
    // Pulsing "Murmure" at bottom
    if ((millis() / 800) % 2 == 0) {
        const char* outro = "Murmure...";
        for (int i = 0; i < strlen(outro); i++) {
            draw_glyph_into_tiles(gTiles, outro[i], 
                                 tft.width()/2 - 40 + i * 10, 
                                 tft.height() - 20, 1.2f, 0, 0x7BEF);
        }
    }
}

// -------------------- Main --------------------

void setup() {
    Serial.begin(115200);
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    
    gTiles.init(tft.width(), tft.height(), TILE_SIZE);
    phaseStartMs = millis();
    
    Serial.println("Fractal Poem - Camera Flyby Optimized");
}

void loop() {
    uint32_t now = millis();
    uint32_t elapsed = now - phaseStartMs;
    
    gTiles.startFrame(0x0000);
    
    switch(currentPhase) {
        case FLYBY:
            if (elapsed < FLYBY_DURATION) {
                float progress = elapsed / (float)FLYBY_DURATION;
                renderFlyby(currentVerse, progress);
            } else {
                currentVerse++;
                if (currentVerse >= NUM_VERSES) {
                    currentPhase = ZOOM_OUT;
                    Serial.println("Starting zoom out");
                }
                phaseStartMs = now;
            }
            break;
            
        case ZOOM_OUT:
            {
                float progress = min(1.0f, elapsed / (float)ZOOM_DURATION);
                renderZoomOut(progress);
                
                if (elapsed >= ZOOM_DURATION) {
                    currentPhase = FINAL;
                    phaseStartMs = now;
                    Serial.println("Final reveal");
                }
            }
            break;
            
        case FINAL:
            renderFinal();
            break;
    }
    
    gTiles.flush(tft);
    
    delay(16); // ~60fps
}
