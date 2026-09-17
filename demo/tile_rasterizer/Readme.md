# FMT Tile Rasterizer (Canonical Implementation)

`FMT_Tile.h` provides a lightweight, modular, dirty-rectangle tile rasterizer designed for embedded displays (e.g. SPI/I2C displays driven by ESP32, STM32, or AVR using LovyanGFX, TFT_eSPI, or custom drivers).

## Key Features

- **Generic Pixel Depth**: Standard template support for RGB565 (`uint16_t`), Grayscale/Paletted (`uint8_t`), or custom pixel formats.
- **DMA-Optimized Memory Allocation**: Automatically attempts `heap_caps_malloc(..., MALLOC_CAP_DMA | MALLOC_CAP_32BIT)` on ESP32 before falling back to standard heap memory.
- **Smart Clear (Dirty Tracking)**: Uses dual flags (`dirty_curr` and `dirty_prev`) to track modified tiles. Non-dirty tiles bypass both memory clear operations and display bus transmissions.
- **Drawing Primitives**: Includes pixel writes/reads, LOD/decimation block writes (`writePixelBlock`), and Fast Bresenham Line Drawing (`drawLine`).
- **Display Agnostic**: Display flushing uses a lambda / function callback (`flush([](x, y, w, h, buf){ ... })`), making it compatible with any graphics library (LovyanGFX, TFT_eSPI, Adafruit_GFX, etc.).

## Practical Usage Example

```cpp
#include "FMT_Tile.h"

// Instantiate RGB565 Tile Manager
FMT::TileManager<uint16_t> tiles;

void setup() {
    // Initialize 240x320 display with 16x16 pixel tiles
    tiles.init(240, 320, 16);
}

void render_frame() {
    // 1. Prepare frame with background color (0x0000)
    tiles.startFrame(0x0000);

    // 2. Draw primitives
    tiles.drawLine(10, 10, 100, 100, 0xF800); // Red line
    tiles.writePixelGlobal(50, 50, 0x07E0);    // Green pixel
    tiles.writePixelBlock(60, 60, 4, 0x001F);  // Blue 4x4 block

    // 3. Flush dirty tiles to display (e.g. LovyanGFX)
    tft.startWrite();
    tiles.flush([](uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t* buf) {
        tft.pushImage(x, y, w, h, buf);
    });
    tft.endWrite();
}
```
