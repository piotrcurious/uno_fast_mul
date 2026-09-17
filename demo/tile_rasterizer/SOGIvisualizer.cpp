#include "SOGIvisualizer.h"
#include <math.h>

// Configuration for a standard SSD1306 SPI OLED using LovyanGFX
class LGFX_SOGI : public lgfx::LGFX_Device {
    lgfx::Panel_SSD1306 _panel_instance;
    lgfx::Bus_SPI        _bus_instance;
public:
    LGFX_SOGI() {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = VSPI_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.pin_sclk = 18;
            cfg.pin_mosi = 23;
            cfg.pin_miso = -1;
            cfg.pin_dc   = 2;
            cfg.dma_channel = 1;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs           = 5;
            cfg.pin_rst          = 4;
            cfg.panel_width      = SOGIVisualizer::SCREEN_WIDTH;
            cfg.panel_height     = SOGIVisualizer::SCREEN_HEIGHT;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            _panel_instance.config(cfg);
        }
        setPanel(&_panel_instance);
    }
};

static LGFX_SOGI& get_hw() {
    static LGFX_SOGI dev;
    return dev;
}

// ------------------ Visualizer Implementation ------------------

static TileManager g_tiled_canvas;
static float last_v_min = -0.1f;
static float last_v_max = 0.1f;

static constexpr int TEXT_ROW_HEIGHT = 0;
static constexpr int ERROR_BAR_Y = SOGIVisualizer::SCREEN_HEIGHT - 1;
static constexpr int WAVE_AREA_HEIGHT = SOGIVisualizer::SCREEN_HEIGHT - TEXT_ROW_HEIGHT - 1;
static constexpr float MIN_RANGE = 0.05f;
static constexpr float PEAK_HISTORY_WEIGHT = 0.95f; // Slower adaptation for stability
static constexpr float PEAK_NEW_WEIGHT = 0.05f;

SOGIVisualizer::SOGIVisualizer() : _canvas(nullptr) {}

void SOGIVisualizer::begin() {
    auto& dev = get_hw();
    dev.init();
    dev.setRotation(0);
    dev.clear();

    gpio_set_drive_capability((gpio_num_t)18, GPIO_DRIVE_CAP_3);
    gpio_set_drive_capability((gpio_num_t)23, GPIO_DRIVE_CAP_3); 
    g_tiled_canvas.init(SCREEN_WIDTH, SCREEN_HEIGHT, TILE_SIZE);
}

void SOGIVisualizer::update(const float* buffer, int bufLen, int startIdx, int count, 
                            float freq, float magnitude, float error) {
    if (count <= 0 || buffer == nullptr) return;
    
    auto& dev = get_hw();
    g_tiled_canvas.startFrame(0);
    
    float range = last_v_max - last_v_min;
    if (range < MIN_RANGE) range = MIN_RANGE;
    
    const int wave_h = WAVE_AREA_HEIGHT;
    const int screen_w = SCREEN_WIDTH;
    const float scale_y = (WAVE_AREA_HEIGHT - 2) / range;
    const float mid_point = (last_v_max + last_v_min) * 0.5f;
    const int center_y = WAVE_AREA_HEIGHT / 2;

    // Draw zero line using fast float math (single multiply)
    int zero_line_y = center_y - (int)lrintf((0.0f - mid_point) * scale_y);
    if (zero_line_y >= 0 && zero_line_y < wave_h) {
        for (int x = 0; x < screen_w; x += 16) {
            g_tiled_canvas.writePixelGlobal(x, zero_line_y, 255);
        }
    }

    // 1. Plot Waveform
    float current_min = 100.0f;
    float current_max = -100.0f;
    int prev_x = -1, prev_y = -1;
    
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        int sample_idx = (startIdx + (x * count / SCREEN_WIDTH)) % bufLen;
        float val = buffer[sample_idx];
        
        if (val < current_min) current_min = val;
        if (val > current_max) current_max = val;
        
        // Calculate Y: (0,0) is top-left in LGFX
        int y = center_y - (int)((val - mid_point) * scale_y);
        if (y < 0) y = 0;
        if (y >= WAVE_AREA_HEIGHT) y = WAVE_AREA_HEIGHT - 1;
        
        if (prev_x != -1) {
            g_tiled_canvas.drawLine(prev_x, prev_y, x, y, 255); // Use 255 for "White" in grayscale
        }
        prev_x = x;
        prev_y = y;
    }
    
    // Smoothly update scale
    last_v_min = (current_min * PEAK_NEW_WEIGHT) + (last_v_min * PEAK_HISTORY_WEIGHT);
    last_v_max = (current_max * PEAK_NEW_WEIGHT) + (last_v_max * PEAK_HISTORY_WEIGHT);
    
    // Flush dirty tiles to display using LovyanGFX
    dev.endWrite();
    dev.startWrite();
    g_tiled_canvas.flush([&dev](uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* buf) {
        dev.pushImage(x, y, w, h, buf, lgfx::grayscale_8bit);
    });
}
