CXX_HOST=g++
CXXFLAGS_HOST=-Wall -O3 -I.. -I.

CXX_AVR=avr-g++
OBJCOPY_AVR=avr-objcopy
MCU=atmega328p
F_CPU=16000000UL
CXXFLAGS_AVR=-Wall -Os -mmcu=$(MCU) -DF_CPU=$(F_CPU) -I.. -I. -DARDUINO

all: test_host test_avr.hex

test_host: test_host.cpp ../arduino_tables_generated.cpp
	$(CXX_HOST) $(CXXFLAGS_HOST) $^ -o $@

test_avr.elf: test_avr.cpp ../arduino_tables_generated.cpp
	$(CXX_AVR) $(CXXFLAGS_AVR) $^ -o $@

test_avr.hex: test_avr.elf
	$(OBJCOPY_AVR) -O ihex $< $@

run_host: test_host
	./test_host

run_avr: test_avr.elf
	timeout 5s simavr -m $(MCU) test_avr.elf || true

clean:
	rm -f test_host *.elf *.hex
