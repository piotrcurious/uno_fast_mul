#ifndef ARDUINO_TABLES_H
#define ARDUINO_TABLES_H
#include <stdint.h>
#ifdef ARDUINO
#include <pgmspace.h>
#else
#ifndef PROGMEM
#define PROGMEM
#endif
#endif

extern const uint8_t PROGMEM msb_table[256];
extern const uint16_t PROGMEM log2_table_q8[256];
extern const uint16_t PROGMEM exp2_table_q8[256];
extern const int16_t PROGMEM sin_table_q15[512];
extern const int16_t PROGMEM cos_table_q15[512];
extern const uint16_t PROGMEM perspective_scale_table_q8[256];
extern const int16_t PROGMEM sphere_theta_sin_q15[128];
extern const int16_t PROGMEM sphere_theta_cos_q15[128];

extern const uint32_t PROGMEM CONST_PI_LOG_Q8;
extern const uint32_t PROGMEM CONST_2PI_LOG_Q8;
extern const int32_t PROGMEM CONST_PI_SIN_Q15;
extern const int32_t PROGMEM CONST_2PI_SIN_Q15;

#endif