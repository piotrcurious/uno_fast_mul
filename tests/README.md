# uno_fast_mul Tests

This directory contains a suite of tests to verify the accuracy and performance of the fast multiplication and division functions.

## Test Suite Overview

1.  **Host-based Fixed-point Exhaustive Test (`host_test.cpp`)**:
    *   Tests all $65535^2$ combinations of 16-bit inputs.
    *   Calculates average and maximum relative error.
    *   Verifies the core logarithmic multiplication algorithm.

2.  **Host-based Floating-point Test (`test_fast_float.cpp`)**:
    *   Verifies the Symmetric Bipartite Table Method (BTM) for 32-bit floats.
    *   Tests multiplication and division.
    *   Provides statistical accuracy reports over 100,000 random samples.

3.  **AVR Emulation Test (`avr_test.c` & `avr_float_test.c`)**:
    *   Cross-compiled for the ATmega328P.
    *   Runs in the `simavr` emulator.
    *   Verifies hardware-specific inline assembly and memory access (`PROGMEM`).

## Prerequisites

To run all tests, you need:
*   `g++` (for host tests)
*   `avr-gcc` and `avr-libc` (for cross-compilation)
*   `simavr` (for emulation)

On Ubuntu/Debian, you can install these with:
```bash
sudo apt-get update && sudo apt-get install -y gcc-avr avr-libc simavr g++
```

## Running Tests

### Automated (Recommended)
From the repository root, run:
```bash
./run_tests.sh
```

### Manual

#### Host Tests
```bash
cd tests
make -f Makefile.host
./host_test          # Fixed-point exhaustive
./test_fast_float    # Floating-point BTM
```

#### AVR Emulation Tests
```bash
cd tests
make -f Makefile.avr
simavr -m atmega328p avr_test.elf
simavr -m atmega328p avr_float_test.elf
```

## Interpreting Results
*   **Average Relative Error**: For fixed-point (Q8.8), expect ~0.5%. For floating-point (BTM), expect ~0.005%.
*   **Exact Matches**: The number of cases where the approximation is identical to the exact result.
*   **Max Relative Error**: The worst-case error encountered during testing.
