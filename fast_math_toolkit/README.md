# Fast Math Toolkit (FMT)

A high-performance fixed-point, 3D math, and display tile rasterizer library for embedded systems, specifically optimized for AVR (e.g., ATmega328P/Arduino Uno) and ESP32.

## Features

- **Modular Architecture**: Separate headers for Core, Fixed-point, Trig, 3D operations, and Tile Rasterization.
- **Log/Exp Pipeline**: Fast approximate multiplication, division, and powers using Q8.8 lookup tables.
- **Q16.16 Arithmetic**: Highly accurate 32-bit fixed-point math.
- **3D Engine Ready**: Vectors, Matrices, Quaternions, Euler rotations, and Perspective projection.
- **Dirty-Tile Rasterizer (`FMT_Tile.h`)**: DMA-capable tile compositor with smart clearing (dual-dirty tracking) to dramatically reduce SPI/I2C display bandwidth.
- **Optimized for AVR & ESP32**: Minimal cycle counts and efficient memory usage.
- **C/C++ Compatible API**: Clean header-only or modular integration.

## Library Structure

- `FMT.h`: Main entry point.
- `FMT_Core.h`: MSB lookup, Log2/Exp2 pipeline, approximate Mul/Div.
- `FMT_Fixed.h`: Q16.16 arithmetic, `inv_sqrt`, and float conversions.
- `FMT_Trig.h`: Sin/Cos wrappers for lookup tables.
- `FMT_3d.h`: 3D primitives and transforms.
- `FMT_Tile.h`: Dirty-rectangle grid tile rasterizer (`Tile`, `TileManager`).
- `FMT_Utils.h`: Miscellaneous utilities (perspective scale, etc.).

## Usage

1. **Generate Tables**: Use the `generator/generate_tables.py` script to produce `arduino_tables_generated.h` and `.cpp`.
2. **Include Headers**: Include `FMT.h` in your project.
3. **Link Tables**: Ensure `arduino_tables_generated.cpp` is compiled and linked.

Example (Math):
```cpp
#include "FMT.h"

// Rotate a vector
FMT::Vec3 v = FMT::vec3_init(0x10000, 0, 0); // (1.0, 0, 0)
FMT::Mat3 R = FMT::mat3_rotation_euler(0, 16384, 0); // 90 deg around Y
FMT::Vec3 vr = FMT::mat3_mul_vec(&R, v);
```

Example (Tile Rasterizer):
```cpp
#include "FMT.h"

FMT::TileManager<uint16_t> tiles;

void setup() {
    tiles.init(240, 320, 16); // 240x320 display, 16x16 tiles
}

void render() {
    tiles.startFrame(0x0000);
    tiles.drawLine(0, 0, 100, 100, 0xF800);
    tiles.flush([](uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t* buf) {
        tft.pushImage(x, y, w, h, buf);
    });
}
```

## Performance

- `sin_u16`: ~64 cycles on AVR.
- `q16_inv_sqrt`: ~257 cycles on AVR.
- `div_u32_u16_ap`: ~395 cycles on AVR (faster than native!).

## License

MIT License
