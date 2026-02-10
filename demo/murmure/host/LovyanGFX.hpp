#ifndef MOCK_LOVYANGFX_HPP
#define MOCK_LOVYANGFX_HPP
#include <stdint.h>
#include <string.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#define VSPI_HOST 0
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
namespace lgfx {
    struct Config_SPI { int spi_host; int spi_mode; int freq_write; int freq_read; int pin_sclk; int pin_mosi; int pin_miso; int pin_dc; };
    struct Config_Panel { int pin_cs; int pin_rst; int pin_busy; int panel_width; int panel_height; int offset_x; int offset_y; int offset_rotation; int dummy_read_pixel; int dummy_read_bits; bool readable; bool invert; bool rgb_order; bool dlen_16bit; bool bus_shared; };
    class Bus_SPI { public: Config_SPI config() { return Config_SPI(); } void config(const Config_SPI& cfg) {} };
    class Panel_Device { public: virtual ~Panel_Device() {} virtual Config_Panel config() { return Config_Panel(); } virtual void config(const Config_Panel& cfg) {} void setBus(Bus_SPI* bus) {} };
    class Panel_ST7789 : public Panel_Device {};
    class LGFX_Device {
    public:
        uint16_t* buffer;
        int _width, _height;
        int _rotation;
        LGFX_Device() : buffer(nullptr), _width(320), _height(240), _rotation(1) {}
        virtual ~LGFX_Device() { if (buffer) delete[] buffer; }
        void setPanel(Panel_Device* panel) {}
        void init() { if (!buffer) buffer = new uint16_t[_width * _height]; memset(buffer, 0, _width * _height * 2); }
        void setRotation(int r) { _rotation = r; }
        void fillScreen(uint16_t color) { if (!buffer) return; for (int i=0; i<_width*_height; ++i) buffer[i] = color; }
        int width() { return _width; }
        int height() { return _height; }
        void pushImage(int x, int y, int w, int h, uint16_t* data) {
            if (!buffer) return;
            for (int j=0; j<h; ++j) {
                for (int i=0; i<w; ++i) {
                    int dx = x + i; int dy = y + j;
                    if (dx >= 0 && dx < _width && dy >= 0 && dy < _height) buffer[dy * _width + dx] = data[j * w + i];
                }
            }
        }
        void savePPM(const std::string& filename) {
            if (!buffer) return;
            std::ofstream ofs(filename, std::ios::binary);
            ofs << "P6\n" << _width << " " << _height << "\n255\n";
            for (int i=0; i<_width*_height; ++i) {
                uint16_t c = buffer[i];
                uint8_t r = ((c >> 11) & 0x1F) << 3; uint8_t g = ((c >> 5) & 0x3F) << 2; uint8_t b = (c & 0x1F) << 3;
                ofs.put(r); ofs.put(g); ofs.put(b);
            }
            ofs.close();
        }
    };
}
#endif
