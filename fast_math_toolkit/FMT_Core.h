#ifndef FMT_CORE_H
#define FMT_CORE_H

#include <stdint.h>

#ifdef ARDUINO
#include <avr/pgmspace.h>
#define FMT_READ8(a, i) pgm_read_byte(&(a)[(i)])
#define FMT_READ16(a, i) pgm_read_word(&(a)[(i)])
#define FMT_READ_S16(a, i) (int16_t)pgm_read_word(&(a)[(i)])
#else
#define FMT_READ8(a, i) (a)[(i)]
#define FMT_READ16(a, i) (a)[(i)]
#define FMT_READ_S16(a, i) (a)[(i)]
#endif

#ifndef INCLUDE_TABLES
#define INCLUDE_TABLES "arduino_tables_generated.h"
#endif
#include INCLUDE_TABLES

namespace FMT {

static inline int fast_msb32(uint32_t v) {
    if (v & 0xFF000000UL) return 24 + FMT_READ8(msb_table, (uint8_t)(v >> 24));
    if (v & 0x00FF0000UL) return 16 + FMT_READ8(msb_table, (uint8_t)(v >> 16));
    if (v & 0x0000FF00UL) return 8  + FMT_READ8(msb_table, (uint8_t)(v >> 8));
    if (v & 0x000000FFUL) return FMT_READ8(msb_table, (uint8_t)v);
    return -1;
}

// Q8.8 Log/Exp pipeline
#ifndef FMT_LOG_Q
#define FMT_LOG_Q 8
#endif

static inline int32_t log2_q8(uint32_t v) {
    if (!v) return -2147483647L - 1L;
    int e;
    uint8_t m;
    if (v & 0xFF000000UL) {
        e = 24 + FMT_READ8(msb_table, (uint8_t)(v >> 24));
        uint8_t s = e - 23;
        m = (uint8_t)((uint16_t)(v >> 16) >> s);
    } else if (v & 0x00FF0000UL) {
        e = 16 + FMT_READ8(msb_table, (uint8_t)(v >> 16));
        uint8_t s = e - 15;
        m = (uint8_t)((uint16_t)(v >> 8) >> s);
    } else if (v & 0x0000FF00UL) {
        e = 8 + FMT_READ8(msb_table, (uint8_t)(v >> 8));
        uint8_t s = e - 7;
        m = (uint8_t)((uint16_t)v >> s);
    } else {
        e = FMT_READ8(msb_table, (uint8_t)v);
        m = (uint8_t)v << (7 - e);
    }
    return ((int32_t)(e - 7) << FMT_LOG_Q) + FMT_READ16(log2_table_q8, m);
}

static inline uint32_t exp2_q8(int32_t y) {
    if (y == (-2147483647L - 1L)) return 0;
    int32_t ip = y >> FMT_LOG_Q;
    uint16_t fr = (uint16_t)(y & 0xFF);
    uint32_t v = FMT_READ16(exp2_table_q8, fr);
    if (ip < 0) {
        uint8_t s = 8 - ip;
        if (s >= 24) return 0;
        if (s >= 16) return (v >> 16) >> (s - 16);
        if (s >= 8)  return (v >> 8) >> (s - 8);
        return v >> s;
    }
    if (ip > 31) return 0xFFFFFFFFUL;
    uint8_t s;
    if (ip >= 8) {
        s = ip - 8;
        if (s >= 16) return (v << 16) << (s - 16);
        if (s >= 8)  return (v << 8) << (s - 8);
        return v << s;
    } else {
        s = 8 - ip;
        return v >> s;
    }
}

static inline uint32_t mul_u16_ap(uint16_t a, uint16_t b) {
    if (!a || !b) return 0;
    return exp2_q8(log2_q8(a) + log2_q8(b));
}

static inline uint32_t div_u32_u16_ap(uint32_t n, uint16_t d) {
    if (!d) return 0xFFFFFFFFUL;
    if (!n) return 0;
    return exp2_q8(log2_q8(n) - log2_q8(d));
}

static inline uint32_t mul_u32_ap(uint32_t a, uint32_t b) {
    if (!a || !b) return 0;
    int32_t la = log2_q8(a);
    int32_t lb = log2_q8(b);
    return exp2_q8(la + lb);
}

static inline uint32_t pow_u32_ap(uint32_t a, float k) {
    if (!a) return 0;
    int32_t la = log2_q8(a);
    // la is Q8.8. k is float. Result should be Q8.8
    return exp2_q8((int32_t)(la * k));
}

} // namespace FMT

#endif
