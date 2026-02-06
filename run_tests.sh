#!/bin/bash

# run_tests.sh - Out-of-box testing script for uno_fast_mul

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}=== uno_fast_mul Test Runner ===${NC}"

# Check for dependencies
check_dep() {
    if ! command -v $1 &> /dev/null; then
        echo -e "${RED}Error: $1 is not installed.${NC}"
        return 1
    fi
    return 0
}

DEPS_MET=true
check_dep g++ || DEPS_MET=false
check_dep avr-gcc || DEPS_MET=false
check_dep simavr || DEPS_MET=false

if [ "$DEPS_MET" = false ]; then
    echo -e "${YELLOW}Some dependencies are missing. Host tests might still work if g++ is present.${NC}"
    echo -e "${YELLOW}To install all dependencies on Ubuntu/Debian:${NC}"
    echo -e "  sudo apt-get update && sudo apt-get install -y gcc-avr avr-libc simavr g++"
fi

# 1. Host-based Fixed-point Exhaustive Test
echo -e "\n${GREEN}1. Running Host-based Fixed-point Exhaustive Test...${NC}"
cd tests
make -f Makefile.host clean
make -f Makefile.host host_test
./host_test | tail -n 10
cd ..

# 2. Host-based Floating-point Test (BTM)
echo -e "\n${GREEN}2. Running Host-based Floating-point Test (BTM)...${NC}"
cd tests
make -f Makefile.host test_fast_float
./test_fast_float | tail -n 10
cd ..

# 3. AVR Emulation Test (Fixed-point)
if command -v simavr &> /dev/null && command -v avr-gcc &> /dev/null; then
    echo -e "\n${GREEN}3. Running AVR Emulation Test (simavr)...${NC}"
    cd tests
    make -f Makefile.avr clean
    make -f Makefile.avr
    echo -e "Running fixed-point test:"
    timeout 5s simavr -m atmega328p avr_test.elf || true
    echo -e "\nRunning floating-point test (BTM):"
    timeout 5s simavr -m atmega328p avr_float_test.elf || true
    echo -e "${GREEN}AVR Emulation Tests completed.${NC}"
    cd ..
fi

# 4. New Fast Math Toolkit Tests
echo -e "\n${GREEN}4. Running New Fast Math Toolkit Tests...${NC}"
cd fast_math_toolkit/tests
make clean
make
echo -e "Running host tests:"
./test_host
if command -v simavr &> /dev/null && command -v avr-gcc &> /dev/null; then
    echo -e "\nRunning AVR benchmarks:"
    make run_avr
fi
cd ../..

echo -e "\n${YELLOW}=== All Tests Completed ===${NC}"
