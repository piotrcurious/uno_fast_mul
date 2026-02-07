#ifndef ARDUINO_TABLES_GENERATED_H
#define ARDUINO_TABLES_GENERATED_H
#include <stdint.h>
#ifdef ARDUINO
#include <avr/pgmspace.h>
#else
#ifndef PROGMEM
#define PROGMEM
#endif
#endif

extern const uint8_t PROGMEM msb_table[256];
#define MSB_TABLE_SIZE 256
extern const uint16_t PROGMEM log2_table_q8[256];
#define LOG2_TABLE_Q8_SIZE 256
extern const uint16_t PROGMEM exp2_table_q8[256];
#define EXP2_TABLE_Q8_SIZE 256
extern const int16_t PROGMEM sin_table_q15[1024];
#define SIN_TABLE_Q15_SIZE 1024
extern const int16_t PROGMEM cos_table_q15[1024];
#define COS_TABLE_Q15_SIZE 1024
extern const uint16_t PROGMEM perspective_scale_table_q8[256];
#define PERSPECTIVE_SCALE_TABLE_Q8_SIZE 256
extern const int16_t PROGMEM sphere_theta_sin_q15[128];
#define SPHERE_THETA_SIN_Q15_SIZE 128
extern const int16_t PROGMEM sphere_theta_cos_q15[128];
#define SPHERE_THETA_COS_Q15_SIZE 128
extern const int16_t PROGMEM atan_slope_table_q15[1024];
#define ATAN_SLOPE_TABLE_Q15_SIZE 1024
extern const uint16_t PROGMEM atan_q15_table[256];
#define ATAN_Q15_TABLE_SIZE 256
extern const uint16_t PROGMEM stereo_radial_table_q12[256];
#define STEREO_RADIAL_TABLE_Q12_SIZE 256
extern const uint16_t PROGMEM lse_table_q8[256];
#define LSE_TABLE_Q8_SIZE 256
extern const uint16_t PROGMEM log2_t1[512];
#define LOG2_T1_SIZE 512
extern const int16_t PROGMEM log2_t2[512];
#define LOG2_T2_SIZE 512
extern const uint16_t PROGMEM exp2_t1[512];
#define EXP2_T1_SIZE 512
extern const int16_t PROGMEM exp2_t2[512];
#define EXP2_T2_SIZE 512

extern const uint32_t PROGMEM CONST_PI_LOG_Q8;
extern const uint32_t PROGMEM CONST_2PI_LOG_Q8;
extern const int32_t PROGMEM CONST_PI_SIN_Q15;
extern const int32_t PROGMEM CONST_2PI_SIN_Q15;

#endif