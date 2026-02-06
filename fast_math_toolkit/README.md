# Fast Math Toolkit (FMT)

A high-performance fixed-point and 3D math library for embedded systems, specifically optimized for AVR (e.g., ATmega328P/Arduino Uno).

## Features

- **Modular Architecture**: Separate headers for Core, Fixed-point, Trig, and 3D operations.
- **Log/Exp Pipeline**: Fast approximate multiplication, division, and powers using Q8.8 lookup tables.
- **Q16.16 Arithmetic**: Highly accurate 32-bit fixed-point math.
- **3D Engine Ready**: Vectors, Matrices, Quaternions, Euler rotations, and Perspective projection.
- **Optimized for AVR**: Minimal cycle counts and PROGMEM usage.
- **C-Compatible API**: Can be used in both C and C++ projects.

## Library Structure

- `FMT.h`: Main entry point.
- `FMT_Core.h`: MSB lookup, Log2/Exp2 pipeline, approximate Mul/Div.
- `FMT_Fixed.h`: Q16.16 arithmetic, `inv_sqrt`, and float conversions.
- `FMT_Trig.h`: Sin/Cos wrappers for lookup tables.
- `FMT_3d.h`: 3D primitives and transforms.
- `FMT_Utils.h`: Miscellaneous utilities (perspective scale, etc.).

## Usage

1. **Generate Tables**: Use the `generator/generate_tables.py` script to produce `arduino_tables_generated.h` and `.cpp`.
2. **Include Headers**: Include `FMT.h` in your project.
3. **Link Tables**: Ensure `arduino_tables_generated.cpp` is compiled and linked.

Example:
```cpp
#include "FMT.h"

// Rotate a vector
FMT::Vec3 v = FMT::vec3_init(0x10000, 0, 0); // (1.0, 0, 0)
FMT::Mat3 R = FMT::mat3_rotation_euler(0, 16384, 0); // 90 deg around Y
FMT::Vec3 vr = FMT::mat3_mul_vec(&R, v);
```

## Performance

- `sin_u16`: ~64 cycles on AVR.
- `q16_inv_sqrt`: ~257 cycles on AVR.
- `div_u32_u16_ap`: ~395 cycles on AVR (faster than native!).

## License

MIT License
