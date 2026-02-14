#ifndef MOCK_ARDUINO_H
#define MOCK_ARDUINO_H
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#define PROGMEM
#define memcpy_P memcpy
#define strcpy_P strcpy
#define pgm_read_ptr(addr) (*(const void**)(addr))
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif
inline void delay(int ms) {}
inline uint32_t millis() {
    static struct timespec start;
    static bool first = true;
    if (first) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        first = false;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
}
inline void yield() {}
struct SerialMock {
    void begin(int baud) {}
    void print(const char* s) { printf("%s", s); }
    void print(int n) { printf("%d", n); }
    void print(float f, int p=2) { printf("%.*f", p, f); }
    void println(const char* s) { printf("%s\n", s); }
    void println(int n) { printf("%d\n", n); }
    void println(float f, int p=2) { printf("%.*f\n", p, f); }
};
extern SerialMock Serial;
#endif
