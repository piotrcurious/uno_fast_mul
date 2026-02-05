/* FastMathToolkit.h - Optimized Fixed-Point Math for Arduino/Embedded
   MIT License | Copyright (c) 2024 */

#ifndef FAST_MATH_TOOLKIT_H
#define FAST_MATH_TOOLKIT_H

#include <stdint.h>
#include <limits.h>

#ifdef ARDUINO
#include <avr/pgmspace.h>
#define FM_READ_U8(a, i) pgm_read_byte(&(a)[(i)])
#define FM_READ_U16(a, i) pgm_read_word(&(a)[(i)])
#define FM_READ_S16(a, i) (int16_t)pgm_read_word(&(a)[(i)])
#else
#define FM_READ_U8(a, i) (a)[(i)]
#define FM_READ_U16(a, i) (a)[(i)]
#define FM_READ_S16(a, i) (a)[(i)]
#endif

#ifndef INCLUDE_TABLES
#define INCLUDE_TABLES "arduino_tables_generated.h"
#endif
#include INCLUDE_TABLES

namespace FastMath {

enum { 
    LOG_Q = 8, 
    SIN_Q = 15, 
    Q16_S = 16, 
    Q16_1 = 1u << Q16_S 
};

// Internal: Leading Zero Count / MSB
static inline int fast_msb32(uint32_t v) {
    if (!v) return -1;
    if (v & 0xFFFF0000u) {
        uint16_t h = (uint16_t)(v >> 16);
        return (h >> 8) ? 24 + FM_READ_U8(msb_table, h >> 8) : 16 + FM_READ_U8(msb_table, h & 0xFF);
    }
    return (v >> 8) ? 8 + FM_READ_U8(msb_table, v >> 8) : FM_READ_U8(msb_table, v & 0xFF);
}

// Internal: Log/Exp Core
static inline int32_t log2_q8(uint32_t v) {
    if (!v) return INT32_MIN;
    int e = fast_msb32(v);
    uint8_t m = (e >= 7) ? (uint8_t)(v >> (e - 7)) : (uint8_t)(v << (7 - e));
    return ((int32_t)(e - 7) << LOG_Q) + FM_READ_U16(log2_table_q8, m);
}

static inline uint32_t exp2_q8(int32_t y) {
    if (y == INT32_MIN) return 0;
    int32_t ip = y >> LOG_Q;
    uint16_t fr = (uint16_t)(y & ((1 << LOG_Q) - 1));
    uint32_t v = FM_READ_U16(exp2_table_q8, fr);
    if (ip >= 0) return (ip > 24) ? UINT32_MAX : (v << ip) >> LOG_Q;
    int s = LOG_Q - ip;
    return (s >= 31) ? 0 : v >> s;
}

// Q16.16 Exact
static inline uint32_t q16_mul_u_ex(uint32_t a, uint32_t b) { return (uint32_t)(((uint64_t)a * b) >> Q16_S); }
static inline int32_t  q16_mul_s_ex(int32_t a, int32_t b)  { return (int32_t)(((int64_t)a * b) >> Q16_S); }
static inline uint32_t q16_div_u_ex(uint32_t a, uint32_t b) { return b ? (uint32_t)(((uint64_t)a << Q16_S) / b) : UINT32_MAX; }
static inline int32_t  q16_div_s_ex(int32_t a, int32_t b)  { 
    if (!b) return (a >= 0) ? INT32_MAX : INT32_MIN;
    return (int32_t)(((int64_t)a << Q16_S) / b);
}

// Q16.16 Approx
static inline uint32_t q16_mul_u_ap(uint32_t a, uint32_t b) {
    if (!a || !b) return 0;
    return exp2_q8(log2_q8(a) + log2_q8(b) - (Q16_S << LOG_Q) + (Q16_S << LOG_Q)); 
}

static inline int32_t q16_mul_s_ap(int32_t a, int32_t b) {
    bool n = (a < 0) ^ (b < 0);
    uint32_t r = q16_mul_u_ap((a < 0 ? -a : a), (b < 0 ? -b : b));
    return n ? -(int32_t)(r & 0x7FFFFFFF) : (int32_t)(r & 0x7FFFFFFF);
}

// Trig
static inline int16_t sin_u16(uint16_t a) {
    uint16_t idx = ((uint32_t)a * (sizeof(sin_table_q15)/2)) >> 16;
    return FM_READ_S16(sin_table_q15, idx);
}

static inline int16_t cos_u16(uint16_t a) {
    uint16_t idx = ((uint32_t)a * (sizeof(cos_table_q15)/2)) >> 16;
    return FM_READ_S16(cos_table_q15, idx);
}

// Projection
static inline uint32_t get_perspective(uint16_t i) {
    uint16_t n = sizeof(perspective_scale_table_q8)/2;
    return FM_READ_U16(perspective_scale_table_q8, (i >= n) ? n - 1 : i);
}

} // namespace FastMath

// C API
extern "C" {
    static inline uint32_t FM_q16_mul_u_ex(uint32_t a, uint32_t b) { return FastMath::q16_mul_u_ex(a, b); }
    static inline uint32_t FM_q16_mul_u_ap(uint32_t a, uint32_t b) { return FastMath::q16_mul_u_ap(a, b); }
    static inline int16_t  FM_sin_u16(uint16_t a) { return FastMath::sin_u16(a); }
}

#endif

