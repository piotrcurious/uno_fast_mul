#ifndef FAST_FLOAT_H
#define FAST_FLOAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __AVR__
#include <avr/pgmspace.h>
#else
#define PROGMEM
#endif

// BTM configuration: n1=4, n2=5, n3=5
// Total index bits = 14
// Table 1: 512 entries
// Table 2: 512 entries
extern const uint16_t PROGMEM log2_t1[512];
extern const int16_t PROGMEM log2_t2[512];

extern const uint16_t PROGMEM exp2_t1[512];
extern const int16_t PROGMEM exp2_t2[512];

float fast_mul_f32(float a, float b);
float fast_div_f32(float a, float b);

#ifdef __cplusplus
}
#endif

#endif
