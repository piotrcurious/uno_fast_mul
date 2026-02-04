// ESP32_TextTransform_Scenes.ino
// Integrated Tile-based Compositor with DMA Queueing

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>

// include generated tables & glyphs
#include "arduino_tables.h"

// ---------------------- Config ----------------------
#define LOG_Q 8
#define SIN_Q 15
#define SIN_SIZE 512
#define TILE_SIZE 64
#define MAX_OUTSTANDING_DMA 4 // Number of tiles to queue before waiting

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
        // Optimization: if clearing to black, use memset
        if (color == 0) memset(buf, 0, n * sizeof(uint16_t));
        else for (size_t i=0; i<n; ++i) buf[i] = color;
        dirty = true; // Mark as dirty so the background is actually drawn
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
                uint16_t w = min((int)tile_size, (int)(screen_w - x0));
                uint16_t h = min((int)tile_size, (int)(screen_h - y0));
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

// ------------------ Globals & Scene State ------------------

TileManager gTiles;

struct TileAnimState {
    float vx, vy;
    float offsetX, offsetY;
};
TileAnimState *tileAnims = nullptr;

enum Scene { SCENE_SCROLL, SCENE_ORBIT, SCENE_WAVE, SCENE_RAIN, SCENE_EXPLODE, SCENE_COUNT };
Scene currentScene = SCENE_SCROLL;
Scene lastScene = SCENE_SCROLL;
uint32_t sceneStartMs = 0;
const uint32_t SCENE_DURATION = 8000;
const uint32_t TRANSITION_MS = 1000;

// ------------------ Fast Math Helpers ------------------

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

void draw_glyph_into_tiles(char ch, int16_t cx, int16_t cy, float scale_f, float angle_rad, uint16_t color) {
    int idx = -1;
    for (uint16_t i = 0; i < GLYPH_COUNT; ++i) {
        if (GLYPH_CHAR_LIST[i] == ch) { idx = i; break; }
    }
    if (idx < 0) return;

    const uint8_t gw = GLYPH_WIDTH;
    const uint8_t gh = GLYPH_HEIGHT;

    uint16_t scale_q8 = (uint16_t)(scale_f * (1 << LOG_Q));
    int16_t py = constrain(cy, 0, (int)tft.height() - 1);
    uint16_t persp_idx = (uint16_t)((py / (float)tft.height()) * 255.0f);
    uint16_t persp_q8 = perspective_scale_table_q8[persp_idx];
    uint32_t combined_scale_q8 = (fast_log_mul_u16(scale_q8, persp_q8) >> LOG_Q);

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
                
                uint32_t sx_scaled_q8 = fast_log_mul_u16((uint16_t)min(asx, 65535UL), (uint16_t)combined_scale_q8) >> LOG_Q;
                uint32_t sy_scaled_q8 = fast_log_mul_u16((uint16_t)min(asy, 65535UL), (uint16_t)combined_scale_q8) >> LOG_Q;
                
                int32_t sxs = (sx_q8 < 0) ? -(int32_t)sx_scaled_q8 : (int32_t)sx_scaled_q8;
                int32_t sys = (sy_q8 < 0) ? -(int32_t)sy_scaled_q8 : (int32_t)sy_scaled_q8;

                int32_t rx_q8 = ( (sxs * (int32_t)cos_q15) - (sys * (int32_t)sin_q15) ) >> 15;
                int32_t ry_q8 = ( (sxs * (int32_t)sin_q15) + (sys * (int32_t)cos_q15) ) >> 15;

                int16_t fx = cx + (int16_t)(rx_q8 >> LOG_Q);
                int16_t fy = cy + (int16_t)(ry_q8 >> LOG_Q);

                gTiles.writePixelGlobal(fx, fy, color);
            }
        }
    }
}

// -------------------- Main Logic --------------------

const char* message = "COMPOSITOR DMA";

void setup() {
    Serial.begin(115200);
    tft.init();
    tft.initDMA();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLUE); //poor mans welcome screen 
    delay(500);
    tft.fillScreen(TFT_BLACK);
    
    gTiles.init(tft.width(), tft.height(), TILE_SIZE);
    
    size_t count = gTiles.cols * gTiles.rows;
    tileAnims = (TileAnimState*)malloc(sizeof(TileAnimState) * count);
    for(int i=0; i<count; i++) {
        tileAnims[i].vx = (random(100) - 50) / 10.0f;
        tileAnims[i].vy = (random(100) - 50) / 10.0f;
    }
    
    sceneStartMs = millis();
}

void loop() {
    uint32_t now = millis();
    uint32_t elapsed = now - sceneStartMs;
    
    // Simple Scene Manager
    if (currentScene != SCENE_EXPLODE && elapsed > SCENE_DURATION) {
        lastScene = currentScene;
        currentScene = SCENE_EXPLODE;
        sceneStartMs = now;
        elapsed = 0;
    } else if (currentScene == SCENE_EXPLODE && elapsed > TRANSITION_MS) {
        currentScene = (Scene)((lastScene + 1) % SCENE_EXPLODE);
        sceneStartMs = now;
        elapsed = 0;
    }

    gTiles.frameClear(0x0000);

    // Calculate Tile Offsets for Explode Transition
    float explodeFactor = 0;
    if (currentScene == SCENE_EXPLODE) {
        explodeFactor = elapsed / (float)TRANSITION_MS;
        if (explodeFactor > 0.5f) explodeFactor = 1.0f - explodeFactor; // reverse
        explodeFactor *= 20.0f; 
    }

    // Render Logic
    int len = strlen(message);
    for (int i = 0; i < len; i++) {
        float t = now * 0.002f;
        int16_t x = (tft.width() / 2) - (len * 20 / 2) + (i * 22);
        int16_t y = (tft.height() / 2);
        
        float scale = 4.5f;
        float angle = 0;

        switch(currentScene) {
            case SCENE_SCROLL:
                x += sinf(t + i * 0.5f) * 30;
                break;
            case SCENE_ORBIT:
                x += cosf(t + i * 0.3f) * 50;
                y += sinf(t + i * 0.3f) * 50;
                angle = t;
                break;
            case SCENE_WAVE:
                y += sinf(t + i * 0.8f) * 40;
                scale = 4.5f + sinf(t + i) * 0.5f;
                break;
            case SCENE_RAIN:
                y = (int)(t * 100 + i * 30) % tft.height();
                break;
        }

        // Apply explosion to the glyph coordinates relative to tiles
        // Note: In a full compositor, we would offset the Tile buffers themselves.
        // Here we shift the drawing to leverage the "Global to Local" mapping.
        if (currentScene == SCENE_EXPLODE) {
            for(int ty=0; ty < gTiles.rows; ty++) {
                for(int tx=0; tx < gTiles.cols; tx++) {
                   // This is a simplified per-tile logic placeholder
                }
            }
        }

        draw_glyph_into_tiles(message[i], x, y, scale, angle, 0xFFFF);
    }

    // High speed flush via DMA Queue
    tft.startWrite();
    gTiles.flush(tft);
    tft.endWrite();
}
