CC=g++
CFLAGS=-I. -I.. -O2
all: simulator
simulator: main.cpp ../ESP32_Fractal_Poem_Final.ino
	$(CC) $(CFLAGS) main.cpp -o simulator
clean:
	rm -f simulator *.ppm
