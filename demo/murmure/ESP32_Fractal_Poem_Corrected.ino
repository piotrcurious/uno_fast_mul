// ESP32_Fractal_Poem_Final.ino
// Verses appear one by one, then camera reveals they form "Le fractal est immense"
// Pixel decimation WITHIN glyphs based on zoom threshold

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>

// include generated tables & glyphs
#include "arduino_tables.h"

// ---------------------- Config ----------------------
#define LOG_Q 8
#define TILE_SIZE 64
#define MAX_OUTSTANDING_DMA 4

TFT_eSPI tft = TFT_eSPI();

// ------------------ Tile-based Compositor ------------------

struct Tile {
    uint16_t x0, y0, w, h;
    uint16_t *buf;
    bool dirty;
    
    Tile(): x0(0), y0(0), w(0), h(0), buf(nullptr), dirty(false) {}
    
    void init(uint16_t _x0, uint16_t _y0, uint16_t _w, uint16_t _h) {
        x0 = _x0; y0 = _y0; w = _w; h = _h;
        size_t n = (size_t)w * (size_t)h;
        if (buf) free(buf);
        buf = (uint16_t*) heap_caps_malloc(n * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (!buf) buf = (uint16_t*) malloc(n * sizeof(uint16_t));
        dirty = false;
        if (buf) memset(buf, 0, n * sizeof(uint16_t));
    }
    
    void clearTo(uint16_t color = 0x0000) {
        if (!buf) return;
        size_t n = (size_t)w * (size_t)h;
        if (color == 0) memset(buf, 0, n * sizeof(uint16_t));
        else for (size_t i=0; i<n; ++i) buf[i] = color;
        dirty = true;
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
    uint16_t screen_w, screen_h, tile_size, cols, rows;
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
        tx = x / tile_size; ty = y / tile_size;
        return true;
    }

    void frameClear(uint16_t bgcolor = 0x0000) {
        for (uint32_t i=0; i<(uint32_t)cols*rows; ++i) tiles[i].clearTo(bgcolor);
    }

    inline void writePixelGlobal(int16_t x, int16_t y, uint16_t color) {
        uint16_t tx, ty;
        if (!coordToTile(x, y, tx, ty)) return;
        Tile &t = tileAtIdx(tx, ty);
        t.writePixelLocal(x - t.x0, y - t.y0, color);
    }

    // Write a block of pixels for decimation
    inline void writePixelBlock(int16_t x, int16_t y, uint8_t blockSize, uint16_t color) {
        for (uint8_t dy = 0; dy < blockSize; dy++) {
            for (uint8_t dx = 0; dx < blockSize; dx++) {
                writePixelGlobal(x + dx, y + dy, color);
            }
        }
    }

    void flush(TFT_eSPI &tft) {
        uint16_t outstanding = 0;
        for (uint32_t i=0; i<(uint32_t)cols*rows; ++i) {
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
        for (size_t i=0; i<(size_t)cols*rows; ++i) tiles[i].freeBuf();
        free(tiles); tiles = nullptr;
    }
};

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

// -------------------- Rendering Pipeline with Decimation --------------------

// Calculate decimation level based on zoom threshold
inline uint8_t getDecimationLevel(float zoom) {
    // Thresholds for decimation activation:
    // zoom > 1.0: no decimation (full detail)
    // zoom 0.5-1.0: 2x decimation
    // zoom 0.25-0.5: 4x decimation
    // zoom 0.125-0.25: 8x decimation
    // zoom < 0.125: 16x decimation
    
    if (zoom >= 1.0f) return 1;
    if (zoom >= 0.5f) return 2;
    if (zoom >= 0.25f) return 4;
    if (zoom >= 0.125f) return 8;
    return 16;
}

void draw_glyph_decimated(TileManager &tiles, char ch, int16_t cx, int16_t cy, 
                          float scale_f, float angle_rad, uint16_t color, uint8_t decimation) {
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

    // Iterate through glyph bitmap with decimation stride
    for (uint8_t col = 0; col < gw; col += decimation) {
        uint32_t colbyte = GLYPH_BITMAPS[idx * gw + col];
        if (!colbyte) continue;
        
        for (uint8_t row = 0; row < gh; row += decimation) {
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

                // Draw pixel block based on decimation level
                if (decimation == 1) {
                    tiles.writePixelGlobal(fx, fy, color);
                } else {
                    tiles.writePixelBlock(fx, fy, decimation, color);
                }
            }
        }
    }
}

// -------------------- Poem Data --------------------

const char* verses[28] = {
    "L'hiver hesite, presage.",
    "L'oiseau se tait, presage.",
    "L'eau moins fraiche, presage.",
    "L'echo s'eloigne, presage.",
    "Murmure, petit murmure.",
    "Fractal de la rupture.",
    "Repete le trouble, repete.",
    "Le monde change en cachette.",
    "Fleur en decembre, presage.",
    "Pluie en ete, presage.",
    "Vent sans saison, presage.",
    "Ciel incertain, presage.",
    "Murmure, petit murmure.",
    "Fractal de la rupture.",
    "Repete le trouble, repete.",
    "Le monde change en cachette.",
    "Rats s'enfuient, presage.",
    "Mensonge use, presage.",
    "Foi qui baisse, presage.",
    "Pouvoir las, presage.",
    "Murmure, petit murmure.",
    "Fractal de la rupture.",
    "Repete le trouble, repete.",
    "Le monde change en cachette.",
    "Chaque nuance, meme instance.",
    "Le grand schema s'avance.",
    "Ecoute bien, enfin sens.",
    "Le fractal est immense."
};

// Pre-calculated positions where verses form "Le fractal est immense"
struct VerseLayout {
    int16_t x, y;
    float angle;
    float scale;
    uint16_t color;
};

const VerseLayout FINAL_LAYOUT[28] = {
    // Verses positioned to trace "Le fractal est immense"
    // L
    {  40, 100,  1.571f, 0.6f, 0x3BFF},
    {  40, 130,  0.000f, 0.6f, 0xF81F},
    // e
    {  68, 100,  0.000f, 0.6f, 0x07FF},
    {  68, 115,  1.571f, 0.6f, 0xFD20},
    // (space)
    // f
    {  96, 100,  1.571f, 0.6f, 0x07E0},
    { 100, 108,  0.000f, 0.6f, 0xFFE0},
    // r
    { 120, 100,  1.571f, 0.6f, 0x3BFF},
    { 124, 108,  0.523f, 0.6f, 0xF81F},
    // a
    { 144, 100,  0.785f, 0.6f, 0x07FF},
    { 152, 130, -0.785f, 0.6f, 0xFD20},
    // c
    { 176, 100,  1.047f, 0.6f, 0x07E0},
    { 172, 130, -1.047f, 0.6f, 0xFFE0},
    // t
    { 200, 100,  1.571f, 0.6f, 0x3BFF},
    { 194, 108,  0.000f, 0.6f, 0xF81F},
    // a
    { 220, 100,  0.785f, 0.6f, 0x07FF},
    { 228, 130, -0.785f, 0.6f, 0xFD20},
    // l
    { 252, 100,  1.571f, 0.6f, 0x07E0},
    // (second row: "est immense")
    // e
    {  40, 150,  0.000f, 0.6f, 0xFFE0},
    {  40, 165,  1.571f, 0.6f, 0x3BFF},
    // s
    {  68, 150,  0.785f, 0.6f, 0xF81F},
    {  68, 180, -0.785f, 0.6f, 0x07FF},
    // t
    {  96, 150,  1.571f, 0.6f, 0xFD20},
    {  90, 158,  0.000f, 0.6f, 0x07E0},
    // Rest of letters
    { 120, 150,  1.571f, 0.6f, 0xFFE0},  // i
    { 144, 150,  1.571f, 0.6f, 0x3BFF},  // m
    { 168, 150,  1.571f, 0.6f, 0xF81F},  // m
    { 192, 150,  0.000f, 0.6f, 0x07FF},  // e
    { 216, 150,  1.571f, 0.6f, 0xFD20}   // ...
};

// -------------------- Scene State --------------------

TileManager gTiles;

enum Phase { 
    FLYBY,        // Camera flies by each verse
    ZOOM_OUT,     // Zoom out to reveal full message
    FINAL         // Show complete message
};

Phase currentPhase = FLYBY;
uint8_t currentVerse = 0;
uint32_t phaseStartMs = 0;
const uint32_t FLYBY_DURATION = 1800;
const uint32_t ZOOM_DURATION = 5000;

float cameraX = 0, cameraY = 0, cameraZoom = 1.0f;

// -------------------- Rendering Functions --------------------

void drawString(TileManager &tiles, const char* str, int16_t x, int16_t y, 
                float scale, float angle, uint16_t color, 
                float camX, float camY, float camZoom, uint8_t decimation) {
    int len = strlen(str);
    for (int i = 0; i < len; i++) {
        int16_t cx = x + i * (int)(10 * scale);
        
        // Apply camera transform
        int16_t screenX = (int16_t)((cx - camX) * camZoom) + tft.width() / 2;
        int16_t screenY = (int16_t)((y - camY) * camZoom) + tft.height() / 2;
        
        draw_glyph_decimated(tiles, str[i], screenX, screenY, 
                           scale * camZoom, angle, color, decimation);
    }
}

void renderFlyby(uint8_t verseIdx, float progress) {
    const VerseLayout &layout = FINAL_LAYOUT[verseIdx];
    
    // Ease in/out
    float eased = progress < 0.5f ? 2 * progress * progress : 1 - pow(-2 * progress + 2, 2) / 2;
    
    // Camera zooms in on this verse
    float targetZoom = 3.0f;
    cameraZoom = 1.0f + (targetZoom - 1.0f) * eased;
    cameraX = layout.x;
    cameraY = layout.y;
    
    // Calculate decimation based on zoom
    uint8_t decimation = getDecimationLevel(cameraZoom);
    
    // Render all verses up to current one
    for (uint8_t i = 0; i <= verseIdx; i++) {
        const VerseLayout &vl = FINAL_LAYOUT[i];
        float alpha = (i == verseIdx) ? eased : 1.0f;
        
        if (alpha > 0.01f) {
            drawString(gTiles, verses[i], vl.x, vl.y, vl.scale * alpha, 
                      vl.angle, vl.color, cameraX, cameraY, cameraZoom, decimation);
        }
    }
}

void renderZoomOut(float progress) {
    // Zoom out from closeup to reveal full composition
    float startZoom = 3.0f;
    float endZoom = 0.08f;
    
    // Exponential zoom out
    cameraZoom = startZoom * pow(endZoom / startZoom, progress);
    
    // Center camera on the message
    cameraX = 160;
    cameraY = 140;
    
    // Calculate decimation based on current zoom level (this is the KEY part!)
    uint8_t decimation = getDecimationLevel(cameraZoom);
    
    // Proximity threshold - at far zoom, only show glyphs near screen center
    float proximityFactor = max(0.0f, (1.0f - progress) * 2.0f - 0.5f);
    
    // Render all verses with pixel decimation
    for (uint8_t i = 0; i < 28; i++) {
        const VerseLayout &vl = FINAL_LAYOUT[i];
        
        // Apply camera transform to verse position
        int16_t screenX = (int16_t)((vl.x - cameraX) * cameraZoom) + tft.width() / 2;
        int16_t screenY = (int16_t)((vl.y - cameraY) * cameraZoom) + tft.height() / 2;
        
        // Distance-based culling with proximity threshold
        float dist = sqrt(pow(screenX - tft.width()/2, 2) + pow(screenY - tft.height()/2, 2));
        float maxDist = 200 * proximityFactor + 500 * (1.0f - proximityFactor);
        
        if (dist < maxDist) {
            drawString(gTiles, verses[i], vl.x, vl.y, vl.scale, 
                      vl.angle, vl.color, cameraX, cameraY, cameraZoom, decimation);
        }
    }
}

void renderFinal() {
    // Fully zoomed out
    cameraX = 160;
    cameraY = 140;
    cameraZoom = 0.08f;
    
    uint8_t decimation = getDecimationLevel(cameraZoom);  // Will be 16x
    
    // Render all verses
    for (uint8_t i = 0; i < 28; i++) {
        const VerseLayout &vl = FINAL_LAYOUT[i];
        drawString(gTiles, verses[i], vl.x, vl.y, vl.scale, 
                  vl.angle, vl.color, cameraX, cameraY, cameraZoom, decimation);
    }
    
    // Pulsing "Murmure" at bottom
    if ((millis() / 800) % 2 == 0) {
        const char* outro = "Murmure...";
        uint8_t dec = 1;  // Full detail for outro text
        for (int i = 0; i < strlen(outro); i++) {
            draw_glyph_decimated(gTiles, outro[i], 
                               tft.width()/2 - 40 + i * 10, 
                               tft.height() - 20, 1.2f, 0, 0x7BEF, dec);
        }
    }
}

// -------------------- Main --------------------

void setup() {
    Serial.begin(115200);
    tft.init();
    tft.initDMA();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    
    gTiles.init(tft.width(), tft.height(), TILE_SIZE);
    phaseStartMs = millis();
    
    Serial.println("Fractal Poem - Pixel Decimation Demo");
    Serial.println("Zoom > 1.0: 1x (full detail)");
    Serial.println("Zoom 0.5-1.0: 2x decimation");
    Serial.println("Zoom 0.25-0.5: 4x decimation");
    Serial.println("Zoom 0.125-0.25: 8x decimation");
    Serial.println("Zoom < 0.125: 16x decimation");
}

void loop() {
    uint32_t now = millis();
    uint32_t elapsed = now - phaseStartMs;
    
    gTiles.frameClear(0x0000);
    
    switch(currentPhase) {
        case FLYBY:
            if (elapsed < FLYBY_DURATION) {
                float progress = elapsed / (float)FLYBY_DURATION;
                renderFlyby(currentVerse, progress);
            } else {
                currentVerse++;
                if (currentVerse >= 28) {
                    currentPhase = ZOOM_OUT;
                    Serial.println("Starting zoom out with pixel decimation");
                }
                phaseStartMs = now;
            }
            break;
            
        case ZOOM_OUT:
            {
                float progress = min(1.0f, elapsed / (float)ZOOM_DURATION);
                renderZoomOut(progress);
                
                // Debug: print decimation level changes
                static uint8_t lastDec = 0;
                uint8_t currentDec = getDecimationLevel(cameraZoom);
                if (currentDec != lastDec) {
                    Serial.print("Decimation: ");
                    Serial.print(currentDec);
                    Serial.print("x at zoom: ");
                    Serial.println(cameraZoom, 3);
                    lastDec = currentDec;
                }
                
                if (elapsed >= ZOOM_DURATION) {
                    currentPhase = FINAL;
                    phaseStartMs = now;
                    Serial.println("Final reveal - Le fractal est immense");
                }
            }
            break;
            
        case FINAL:
            renderFinal();
            break;
    }
    
    tft.startWrite();
    gTiles.flush(tft);
    tft.endWrite();
    
    delay(16); // ~60fps
}
