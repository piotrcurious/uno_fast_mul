#ifndef SOGI_VISUALIZER_H
#define SOGI_VISUALIZER_H

#include <Arduino.h>

// Include LovyanGFX
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// Include FMT Tile Rasterizer
#include "../../fast_math_toolkit/FMT_Tile.h"

// --- Configuration for Tile-based Compositor ---
#define TILE_SIZE 4

// Convenience aliases using FMT Tile Rasterizer
using Tile = FMT::Tile<uint8_t>;
using TileManager = FMT::TileManager<uint8_t>;

/**
 * @brief Class to handle visualization of SOGI-PLL data
 */
class SOGIVisualizer {
public:
    static constexpr int SCREEN_WIDTH = 128;
    static constexpr int SCREEN_HEIGHT = 64;

    SOGIVisualizer();
    void begin();
    void update(const float* buffer, int bufLen, int startIdx, int count, 
                float freq, float magnitude, float error);

private:
    LGFX_Sprite _canvas; // Use a sprite for flicker-free double buffering
};

#endif // SOGI_VISUALIZER_H
