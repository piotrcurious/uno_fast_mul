#ifndef FMT_TILE_H
#define FMT_TILE_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <new>

// Handle platform differences for Arduino / ESP32 vs Standard C++ / Host test
#if defined(ESP32) || defined(ARDUINO)
  #include <Arduino.h>
  #if defined(ESP32)
    #include <esp_heap_caps.h>
  #endif
#else
  #include <algorithm>
  using std::min;
  using std::max;
#endif

namespace FMT {

/**
 * @brief Tile representation for dirty-rectangle grid rendering.
 * Supports configurable pixel depth (e.g. uint16_t for RGB565 or uint8_t for grayscale / paletted).
 */
template <typename PixelT = uint16_t>
struct Tile {
    uint16_t x0, y0;   ///< Upper-left screen coordinate of tile
    uint16_t w, h;     ///< Width and height of tile in pixels
    PixelT *buf;       ///< Framebuffer memory for tile pixels
    bool dirty_curr;   ///< Modified in current frame
    bool dirty_prev;   ///< Modified in previous frame

    Tile() : x0(0), y0(0), w(0), h(0), buf(nullptr), dirty_curr(false), dirty_prev(false) {}

    ~Tile() {
        freeBuf();
    }

    /**
     * @brief Initialize tile dimensions and allocate framebuffer.
     * Uses ESP32 DMA capability if available, falling back to standard heap.
     */
    void init(uint16_t _x0, uint16_t _y0, uint16_t _w, uint16_t _h) {
        x0 = _x0; y0 = _y0; w = _w; h = _h;
        size_t pixel_count = (size_t)w * (size_t)h;
        freeBuf();

#if defined(ESP32)
        buf = (PixelT*) heap_caps_malloc(pixel_count * sizeof(PixelT), MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
        if (!buf) {
            buf = (PixelT*) heap_caps_malloc(pixel_count * sizeof(PixelT), MALLOC_CAP_8BIT);
        }
#endif
        if (!buf) {
            buf = (PixelT*) malloc(pixel_count * sizeof(PixelT));
        }

        dirty_curr = false;
        dirty_prev = false;
        if (buf) {
            memset(buf, 0, pixel_count * sizeof(PixelT));
        }
    }

    /**
     * @brief Free tile framebuffer allocation.
     */
    void freeBuf() {
        if (buf) {
            free(buf);
            buf = nullptr;
        }
    }

    /**
     * @brief Prepare tile for a new frame.
     * Implements "smart clearing": only clears the buffer if it was dirty in the previous frame or current frame.
     * Resets dirty flags to track new writes in the upcoming frame.
     */
    void prepareFrame(PixelT bgcolor = 0) {
        if (dirty_prev || dirty_curr) {
            size_t n = (size_t)w * (size_t)h;
            if (buf) {
                if (bgcolor == 0) {
                    memset(buf, 0, n * sizeof(PixelT));
                } else {
                    for (size_t i = 0; i < n; ++i) buf[i] = bgcolor;
                }
            }
        }
        dirty_prev = dirty_curr;
        dirty_curr = false;
    }

    /**
     * @brief Write pixel using tile-local coordinates.
     */
    inline void writePixelLocal(int16_t lx, int16_t ly, PixelT color) {
        if (!buf || lx < 0 || ly < 0 || lx >= (int16_t)w || ly >= (int16_t)h) return;
        buf[(size_t)ly * (size_t)w + (size_t)lx] = color;
        dirty_curr = true;
    }

    /**
     * @brief Read pixel from tile-local coordinates.
     */
    inline PixelT readPixelLocal(int16_t lx, int16_t ly) const {
        if (!buf || lx < 0 || ly < 0 || lx >= (int16_t)w || ly >= (int16_t)h) return 0;
        return buf[(size_t)ly * (size_t)w + (size_t)lx];
    }
};

/**
 * @brief Manages a grid of Tiles to minimize display SPI/DMA transfer traffic.
 */
template <typename PixelT = uint16_t>
class TileManager {
public:
    uint16_t screen_w, screen_h;
    uint16_t tile_size;
    uint16_t cols, rows;
    Tile<PixelT> *tiles;

    TileManager() : screen_w(0), screen_h(0), tile_size(0), cols(0), rows(0), tiles(nullptr) {}

    ~TileManager() {
        deinit();
    }

    /**
     * @brief Initialize tile grid for given screen resolution and tile size.
     */
    void init(uint16_t sw, uint16_t sh, uint16_t tsize = 16) {
        deinit();
        screen_w = sw;
        screen_h = sh;
        tile_size = tsize;
        cols = (screen_w + tile_size - 1) / tile_size;
        rows = (screen_h + tile_size - 1) / tile_size;
        size_t count = (size_t)cols * rows;

        tiles = (Tile<PixelT>*) malloc(sizeof(Tile<PixelT>) * count);
        for (uint16_t r = 0; r < rows; ++r) {
            for (uint16_t c = 0; c < cols; ++c) {
                uint16_t x0 = c * tile_size;
                uint16_t y0 = r * tile_size;
                uint16_t w = (x0 + tile_size <= screen_w) ? tile_size : (screen_w - x0);
                uint16_t h = (y0 + tile_size <= screen_h) ? tile_size : (screen_h - y0);

                Tile<PixelT> &t = tiles[r * cols + c];
                new (&t) Tile<PixelT>();
                t.init(x0, y0, w, h);
            }
        }
    }

    /**
     * @brief Deallocate all tile memory.
     */
    void deinit() {
        if (tiles) {
            size_t count = (size_t)cols * rows;
            for (size_t i = 0; i < count; ++i) {
                tiles[i].~Tile<PixelT>();
            }
            free(tiles);
            tiles = nullptr;
        }
        screen_w = screen_h = tile_size = cols = rows = 0;
    }

    /**
     * @brief Get pointer to tile at tile grid coordinates.
     */
    inline Tile<PixelT>* tileAtIdx(uint16_t tx, uint16_t ty) {
        if (tx >= cols || ty >= rows || !tiles) return nullptr;
        return &tiles[ty * cols + tx];
    }

    /**
     * @brief Convert global screen coordinates to tile grid indices.
     */
    inline bool coordToTile(int16_t x, int16_t y, uint16_t &tx, uint16_t &ty) const {
        if (x < 0 || y < 0 || x >= (int16_t)screen_w || y >= (int16_t)screen_h) return false;
        tx = (uint16_t)(x / tile_size);
        ty = (uint16_t)(y / tile_size);
        return true;
    }

    /**
     * @brief Begin a new frame with optional background color.
     */
    void startFrame(PixelT bgcolor = 0) {
        uint32_t count = (uint32_t)cols * rows;
        for (uint32_t i = 0; i < count; ++i) {
            tiles[i].prepareFrame(bgcolor);
        }
    }

    /**
     * @brief Write pixel using global screen coordinates.
     */
    inline void writePixelGlobal(int16_t x, int16_t y, PixelT color) {
        uint16_t tx, ty;
        if (!coordToTile(x, y, tx, ty)) return;
        Tile<PixelT> &t = tiles[ty * cols + tx];
        t.writePixelLocal(x - t.x0, y - t.y0, color);
    }

    /**
     * @brief Read pixel from global screen coordinates.
     */
    inline PixelT readPixelGlobal(int16_t x, int16_t y) const {
        uint16_t tx, ty;
        if (!coordToTile(x, y, tx, ty)) return 0;
        const Tile<PixelT> &t = tiles[ty * cols + tx];
        return t.readPixelLocal(x - t.x0, y - t.y0);
    }

    /**
     * @brief Write a rectangular block of pixels (useful for decimation/LOD rendering).
     */
    inline void writePixelBlock(int16_t x, int16_t y, uint8_t blockSize, PixelT color) {
        for (uint8_t dy = 0; dy < blockSize; dy++) {
            for (uint8_t dx = 0; dx < blockSize; dx++) {
                writePixelGlobal(x + dx, y + dy, color);
            }
        }
    }

    /**
     * @brief Bresenham line drawing algorithm on the tiled canvas.
     */
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, PixelT color) {
        if ((x0 < 0 && x1 < 0) || (x0 >= (int16_t)screen_w && x1 >= (int16_t)screen_w) ||
            (y0 < 0 && y1 < 0) || (y0 >= (int16_t)screen_h && y1 >= (int16_t)screen_h)) {
            return;
        }

        int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy, e2;

        while (true) {
            writePixelGlobal(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    /**
     * @brief Flush dirty tiles to display using a user-provided callback function.
     * @param pushTileFunc Callback with signature `void(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const PixelT* buf)`
     */
    template <typename Func>
    void flush(Func pushTileFunc) {
        uint32_t count = (uint32_t)cols * rows;
        for (uint32_t i = 0; i < count; ++i) {
            Tile<PixelT> &t = tiles[i];
            if (t.dirty_curr || t.dirty_prev) {
                pushTileFunc(t.x0, t.y0, t.w, t.h, t.buf);
            }
        }
    }
};

} // namespace FMT

#endif // FMT_TILE_H
