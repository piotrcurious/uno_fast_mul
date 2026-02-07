// ------------------ Tile-based Compositor Structures (tile-aware) ------------------

#define TILE_SIZE 4

struct Tile {
    uint16_t x0, y0;
    uint16_t w, h;
    uint16_t stride;   // 32-bit aligned width
    uint8_t *buf;
    bool dirty_curr;
    bool dirty_prev;

    Tile(): x0(0), y0(0), w(0), h(0), stride(0), buf(nullptr), dirty_curr(false), dirty_prev(false) {}

    void init(uint16_t _x0, uint16_t _y0, uint16_t _w, uint16_t _h) {
        x0 = _x0; y0 = _y0; w = _w; h = _h;
        stride = (w + 3) & ~3;
        size_t n = (size_t)stride * h;
        if (buf) heap_caps_free(buf);
        buf = (uint8_t*) heap_caps_malloc(n, MALLOC_CAP_DMA | MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        dirty_curr = false;
        dirty_prev = false;
        if (buf) memset(buf, 0, n);
    }

    void prepareFrame() {
        if (dirty_prev || dirty_curr) {
            if (buf) memset(buf, 0, (size_t)stride * h);
        }
        dirty_prev = dirty_curr;
        dirty_curr = false;
    }

    inline void writePixelLocal(int16_t lx, int16_t ly, uint8_t color) {
        if (!buf || lx < 0 || ly < 0 || lx >= (int)w || ly >= (int)h) return;
        buf[ly * stride + lx] = color;
        dirty_curr = true;
    }
};

struct TileManager {
    uint16_t screen_w, screen_h;
    uint16_t tile_size;
    uint16_t cols, rows;
    Tile *tiles;

    TileManager(): tiles(nullptr) {}

    void init(uint16_t sw, uint16_t sh, uint16_t tsize) {
        screen_w = sw; screen_h = sh; tile_size = tsize;
        cols = (screen_w + tile_size - 1) / tile_size;
        rows = (screen_h + tile_size - 1) / tile_size;
        tiles = (Tile*)malloc(sizeof(Tile) * cols * rows);

        for (uint16_t r=0; r<rows; ++r) {
            for (uint16_t c=0; c<cols; ++c) {
                uint16_t x0 = c * tile_size;
                uint16_t y0 = r * tile_size;
                uint16_t tw = (uint16_t)fmin((float)tile_size, (float)(screen_w - x0));
                uint16_t th = (uint16_t)fmin((float)tile_size, (float)(screen_h - y0));
                Tile &t = tiles[r*cols + c];
                new (&t) Tile();
                t.init(x0, y0, tw, th);
            }
        }
    }

    inline Tile* tileAt(int tx, int ty) {
        if ((uint32_t)tx >= cols || (uint32_t)ty >= rows) return nullptr;
        return &tiles[ty * cols + tx];
    }

    // Fast single-pixel write using precomputed tile/local coords (no division)
    inline void writePixelWithTileLocal(int tx, int ty, int lx, int ly, uint8_t color) {
        Tile* t = tileAt(tx, ty);
        if (!t) return;
        // bounds check within tile (should be fast)
        if ((uint32_t)lx >= t->w || (uint32_t)ly >= t->h) return;
        t->buf[ly * t->stride + lx] = color;
        t->dirty_curr = true;
    }

    // Fast horizontal run plotting: write [x0..x1] at row y (inclusive).
    // Avoids division inside the inner loop; iterates tile-by-tile and writes contiguous blocks.
    void plotHorizontalRun(int x0, int y, int x1, uint8_t color) {
        if (y < 0 || y >= (int)screen_h) return;
        if (x1 < x0) return;

        // clamp run to screen
        if (x0 < 0) x0 = 0;
        if (x1 >= (int)screen_w) x1 = screen_w - 1;

        int tx = x0 / tile_size;
        int ty = y / tile_size;
        int local_y = y - ty * tile_size;
        int cur_x = x0;

        while (cur_x <= x1) {
            // current tile width (last column may be smaller)
            int cur_tile_w = (tx == (int)cols - 1) ? (screen_w - tx * tile_size) : tile_size;
            int local_x = cur_x - tx * tile_size;
            int max_in_tile = cur_tile_w - local_x;
            int remaining = x1 - cur_x + 1;
            int len = (remaining < max_in_tile) ? remaining : max_in_tile;

            Tile* t = tileAt(tx, ty);
            if (t && t->buf) {
                uint8_t* dst = t->buf + local_y * t->stride + local_x;
                // write 'len' bytes
                for (int k = 0; k < len; ++k) dst[k] = color;
                t->dirty_curr = true;
            }

            cur_x += len;
            if (cur_x <= x1) ++tx; // advance to next tile horizontally
        }
    }

    // Tile-aware Bresenham: updates local tile coords instead of doing division per pixel.
    void drawLine(int x0, int y0, int x1, int y1, uint8_t color) {
        // trivial clipping: if completely outside screen, return (simple conservative test)
        if ((x0 < 0 && x1 < 0) || (x0 >= (int)screen_w && x1 >= (int)screen_w)
         || (y0 < 0 && y1 < 0) || (y0 >= (int)screen_h && y1 >= (int)screen_h)) return;

        // clamp endpoints to screen bounds (so algorithm doesn't walk off)
        if (x0 < 0) x0 = 0;
        if (x0 >= (int)screen_w) x0 = screen_w - 1;
        if (x1 < 0) x1 = 0;
        if (x1 >= (int)screen_w) x1 = screen_w - 1;
        if (y0 < 0) y0 = 0;
        if (y0 >= (int)screen_h) y0 = screen_h - 1;
        if (y1 < 0) y1 = 0;
        if (y1 >= (int)screen_h) y1 = screen_h - 1;

        int dx = abs(x1 - x0);
        int sx = x0 < x1 ? 1 : -1;
        int dy = -abs(y1 - y0);
        int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;

        // initialize tile/local coordinates (single division per start)
        int tx = x0 / tile_size;
        int ty = y0 / tile_size;
        int lx = x0 - tx * tile_size;
        int ly = y0 - ty * tile_size;

        // current tile logical width/height for boundary handling (last tile shorter)
        auto cur_tile_w = [&](int cur_tx)->int { return (cur_tx == (int)cols - 1) ? (screen_w - cur_tx * tile_size) : tile_size; };
        auto cur_tile_h = [&](int cur_ty)->int { return (cur_ty == (int)rows - 1) ? (screen_h - cur_ty * tile_size) : tile_size; };

        // main loop
        int x = x0, y = y0;
        while (true) {
            // set pixel in current tile
            Tile* t = tileAt(tx, ty);
            if (t && t->buf && (uint32_t)lx < (uint32_t)t->w && (uint32_t)ly < (uint32_t)t->h) {
                t->buf[ly * t->stride + lx] = color;
                t->dirty_curr = true;
            }

            if (x == x1 && y == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { // step in x
                err += dy;
                x += sx;
                lx += sx;
                // handle horizontal tile crossing
                int tile_w = cur_tile_w(tx);
                if (lx >= tile_w) {
                    // moved right out of current tile
                    lx -= tile_w; // enters new tile at 0
                    ++tx;
                } else if (lx < 0) {
                    // moved left out of current tile
                    --tx;
                    int new_w = cur_tile_w(tx);
                    lx += new_w; // wrap to rightmost pixel of new tile
                }
            }
            if (e2 <= dx) { // step in y
                err += dx;
                y += sy;
                ly += sy;
                // handle vertical tile crossing
                int tile_h = cur_tile_h(ty);
                if (ly >= tile_h) {
                    ly -= tile_h;
                    ++ty;
                } else if (ly < 0) {
                    --ty;
                    int new_h = cur_tile_h(ty);
                    ly += new_h;
                }
            }
        }
    }

    // existing API kept for compatibility (calls tile-aware path)
    inline void startFrame() {
        uint32_t count = (uint32_t)cols * rows;
        for (uint32_t i=0; i<count; ++i) tiles[i].prepareFrame();
    }

    inline void flush(LGFX_SOGI &dev) {
        dev.startWrite();
        for (uint32_t i=0; i < (uint32_t)cols * rows; ++i) {
            Tile &t = tiles[i];
            if (t.dirty_curr || t.dirty_prev) {
                dev.waitDMA();
                dev.pushImageDMA(t.x0, t.y0, t.w, t.h, t.buf);
            }
        }
        dev.endWrite();
    }

    // keep old API but faster
    inline void writePixelGlobal(int16_t x, int16_t y, uint8_t color) {
        if (x < 0 || y < 0 || x >= (int)screen_w || y >= (int)screen_h) return;
        int tx = x / tile_size;
        int ty = y / tile_size;
        int lx = x - tx * tile_size;
        int ly = y - ty * tile_size;
        writePixelWithTileLocal(tx, ty, lx, ly, color);
    }
};
