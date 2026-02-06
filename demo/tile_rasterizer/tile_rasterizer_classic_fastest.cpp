// ------------------ Tile-based Compositor Structures ------------------

#define TILE_SIZE 4 

struct Tile {
    uint16_t x0, y0;   
    uint16_t w, h;     
    uint8_t *buf;      
    bool dirty_curr;   
    bool dirty_prev;   
    
    Tile(): x0(0), y0(0), w(0), h(0), buf(nullptr), dirty_curr(false), dirty_prev(false) {}
    
    void init(uint16_t _x0, uint16_t _y0, uint16_t _w, uint16_t _h) {
        x0 = _x0; y0 = _y0; w = _w; h = _h;
        size_t n = (size_t)w * (size_t)h; // 1 byte per pixel for internal compositing
        if (buf) free(buf);
        
        buf = (uint8_t*) heap_caps_malloc(n, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (!buf) buf = (uint8_t*) malloc(n); 
        
        dirty_curr = true; // Initial frame is dirty
        dirty_prev = true;
        if (buf) memset(buf, 0, n);
    }
    
    void prepareFrame() {
        // Only clear if it was dirty, to save CPU cycles
        if (dirty_prev || dirty_curr) {
            if (buf) memset(buf, 0, (size_t)w * h);
        }
        dirty_prev = dirty_curr;
        dirty_curr = false;
    }

    inline void writePixelLocal(int16_t lx, int16_t ly, uint8_t color) {
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

    void init(uint16_t sw, uint16_t sh, uint16_t tsize) {
        screen_w = sw; screen_h = sh; tile_size = tsize;
        cols = (screen_w + tile_size - 1) / tile_size;
        rows = (screen_h + tile_size - 1) / tile_size;
        tiles = (Tile*)malloc(sizeof(Tile) * cols * rows);
        
        for (uint16_t r=0; r<rows; ++r) {
            for (uint16_t c=0; c<cols; ++c) {
                uint16_t x0 = c * tile_size;
                uint16_t y0 = r * tile_size;
                uint16_t tw = (x0 + tile_size <= screen_w) ? tile_size : (screen_w - x0);
                uint16_t th = (y0 + tile_size <= screen_h) ? tile_size : (screen_h - y0);
                Tile &t = tiles[r*cols + c];
                new (&t) Tile();
                t.init(x0, y0, tw, th);
            }
        }
    }

    inline void writePixelGlobal(int16_t x, int16_t y, uint8_t color) {
        if (x < 0 || y < 0 || x >= (int)screen_w || y >= (int)screen_h) return;
        uint16_t tx = x / tile_size;
        uint16_t ty = y / tile_size;
        tiles[ty * cols + tx].writePixelLocal(x - (tx * tile_size), y - (ty * tile_size), color);
    }

    void drawLine(int x0, int y0, int x1, int y1, uint8_t color) {
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

    void startFrame() {
        uint32_t count = (uint32_t)cols * rows;
        for (uint32_t i=0; i<count; ++i) tiles[i].prepareFrame();
    }

    void flush(LGFX_SOGI &dev) {
        dev.startWrite();
        for (uint32_t i=0; i < (uint32_t)cols * rows; ++i) {
            Tile &t = tiles[i];
            if (t.dirty_curr || t.dirty_prev) {
                // FIX: Use grayscale_8bit format to ensure 1 is interpreted as visible
                // and 0 as black. The SSD1306 driver in LovyanGFX will handle the
                // bit-packing automatically from this 8-bit source.
                dev.pushImage(t.x0, t.y0, t.w, t.h, t.buf, lgfx::grayscale_8bit);
            }
        }
        dev.endWrite();
    }
};
