// ------------------ Tile-based Compositor Structures (fixed lifecycle + faster writes) ------------------

#define TILE_SIZE 4

struct Tile {
    uint16_t x0, y0;
    uint16_t w, h;
    uint16_t stride;   // 32-bit aligned width
    uint8_t *buf;
    bool dirty_curr;   // tile written in current frame (non-zero content)
    bool dirty_prev;   // tile had non-zero content at the end of previous frame (requires clearing before next frame)
    bool cleared;      // we cleared this tile at startFrame to erase previous content (must be pushed once)

    Tile(): x0(0), y0(0), w(0), h(0), stride(0), buf(nullptr),
            dirty_curr(false), dirty_prev(false), cleared(false) {}

    void init(uint16_t _x0, uint16_t _y0, uint16_t _w, uint16_t _h) {
        x0 = _x0; y0 = _y0; w = _w; h = _h;
        stride = (w + 3) & ~3;
        size_t n = (size_t)stride * h;
        if (buf) heap_caps_free(buf);
        buf = (uint8_t*) heap_caps_malloc(n, MALLOC_CAP_DMA | MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        dirty_curr = false;
        dirty_prev = false;
        cleared = false;
        if (buf) memset(buf, 0, n);
    }

    inline void clearBuf() {
        if (buf) memset(buf, 0, (size_t)stride * h);
    }

    inline void prepareForNewFrame() {
        // If the tile had non-zero content at the end of the previous frame,
        // clear it now (so remnants are not visible while we redraw).
        if (dirty_prev) {
            clearBuf();
            cleared = true;      // we must push this cleared tile once to the display (erase on-screen)
        } else {
            cleared = false;
        }
        // reset current-frame draw flag; dirty_prev will be set after flush based on actual drawing
        dirty_curr = false;
        // note: dirty_prev is cleared here to avoid repeated clears until we set it again after flush
        dirty_prev = false;
    }

    inline void markWritten(uint16_t lx, uint16_t ly, uint8_t color) {
        // direct write without extra bounds-checking when caller guarantees valid lx/ly
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

    // Fast single-pixel write using precomputed tile/local coords (no division inside hot path)
    inline void writePixelWithTileLocal(int tx, int ty, int lx, int ly, uint8_t color) {
        Tile* t = tileAt(tx, ty);
        if (!t || !t->buf) return;
        // bounds check within tile (cheap)
        if ((uint32_t)lx >= t->w || (uint32_t)ly >= t->h) return;
        t->buf[ly * t->stride + lx] = color;
        t->dirty_curr = true;
    }

    // Fast horizontal run plotting: write [x0..x1] at row y (inclusive).
    // Uses memset for contiguous bytes inside tile (faster than byte loop).
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
                // faster contiguous write
                memset(dst, color, (size_t)len);
                t->dirty_curr = true;
            }

            cur_x += len;
            if (cur_x <= x1) ++tx; // advance to next tile horizontally
        }
    }

    // Tile-aware Bresenham that writes pixels directly into tile buffers.
    // No per-pixel division: single division only for starting tile coords.
    void drawLine(int x0, int y0, int x1, int y1, uint8_t color) {
        // trivial bounding out test
        if ((x0 < 0 && x1 < 0) || (x0 >= (int)screen_w && x1 >= (int)screen_w)
         || (y0 < 0 && y1 < 0) || (y0 >= (int)screen_h && y1 >= (int)screen_h)) return;

        // clamp endpoints to screen bounds
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

        // starting tile/local coords (one division)
        int tx = x0 / tile_size;
        int ty = y0 / tile_size;
        int lx = x0 - tx * tile_size;
        int ly = y0 - ty * tile_size;

        // helpers for last tile sizes (cheap lambdas)
        auto tile_w_at = [&](int cur_tx)->int { return (cur_tx == (int)cols - 1) ? (screen_w - cur_tx * tile_size) : tile_size; };
        auto tile_h_at = [&](int cur_ty)->int { return (cur_ty == (int)rows - 1) ? (screen_h - cur_ty * tile_size) : tile_size; };

        int x = x0, y = y0;
        while (true) {
            // fast write into tile buffer if available
            Tile* t = tileAt(tx, ty);
            if (t && t->buf) {
                // these bounds checks should be redundant but safe
                if ((uint32_t)lx < (uint32_t)t->w && (uint32_t)ly < (uint32_t)t->h) {
                    t->buf[ly * t->stride + lx] = color;
                    t->dirty_curr = true;
                }
            }

            if (x == x1 && y == y1) break;

            int e2 = 2 * err;
            if (e2 >= dy) { // step in x
                err += dy;
                x += sx;
                lx += sx;
                // horizontal tile crossing handling
                int cur_tile_w = tile_w_at(tx);
                if (lx >= cur_tile_w) {
                    // moved right out of current tile
                    lx -= cur_tile_w;
                    ++tx;
                } else if (lx < 0) {
                    // moved left out of current tile
                    --tx;
                    int new_w = tile_w_at(tx);
                    lx += new_w;
                }
            }
            if (e2 <= dx) { // step in y
                err += dx;
                y += sy;
                ly += sy;
                int cur_tile_h = tile_h_at(ty);
                if (ly >= cur_tile_h) {
                    ly -= cur_tile_h;
                    ++ty;
                } else if (ly < 0) {
                    --ty;
                    int new_h = tile_h_at(ty);
                    ly += new_h;
                }
            }
        }
    }

    // --- lifecycle: startFrame clears tiles that had non-zero content last frame (dirty_prev) ---
    inline void startFrame() {
        uint32_t count = (uint32_t)cols * rows;
        for (uint32_t i=0; i<count; ++i) {
            Tile &t = tiles[i];
            // clear only those tiles that were non-zero at the end of last frame
            t.prepareForNewFrame();
        }
    }

    // flush: push tiles that were either cleared at startFrame (to erase on-screen)
    // or were written into this frame (dirty_curr). After pushing, set dirty_prev
    // = dirty_curr (we only want to remember tiles that now contain non-zero pixels).
    inline void flush(LGFX_SOGI &dev) {
        dev.startWrite();
        uint32_t total = (uint32_t)cols * rows;
        for (uint32_t i=0; i < total; ++i) {
            Tile &t = tiles[i];
            if (t.cleared || t.dirty_curr) {
                dev.waitDMA();
                dev.pushImageDMA(t.x0, t.y0, t.w, t.h, t.buf);
            }
        }
        dev.endWrite();

        // update dirty_prev/dirty_curr/cleared for next frame:
        for (uint32_t i=0; i < total; ++i) {
            Tile &t = tiles[i];
            // Only tiles that have non-zero content after flush should be remembered as dirty_prev.
            // cleared-only tiles (we pushed zeros) should NOT be set as dirty_prev.
            t.dirty_prev = t.dirty_curr;
            t.dirty_curr = false;
            t.cleared = false;
        }
    }

    // compatibility wrapper (keeps the old writePixelGlobal API but now faster internals)
    inline void writePixelGlobal(int16_t x, int16_t y, uint8_t color) {
        if (x < 0 || y < 0 || x >= (int)screen_w || y >= (int)screen_h) return;
        int tx = x / tile_size;
        int ty = y / tile_size;
        int lx = x - tx * tile_size;
        int ly = y - ty * tile_size;
        writePixelWithTileLocal(tx, ty, lx, ly, color);
    }
};
