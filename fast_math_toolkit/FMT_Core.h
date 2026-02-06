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
    if (!v) return -1;
    if (v & 0xFFFF0000u) {
        uint16_t h = (uint16_t)(v >> 16);
        return (h >> 8) ? 24 + FMT_READ8(msb_table, h >> 8) : 16 + FMT_READ8(msb_table, h & 0xFF);
    }
    return (v >> 8) ? 8 + FMT_READ8(msb_table, v >> 8) : FMT_READ8(msb_table, v & 0xFF);
}

// Q8.8 Log/Exp pipeline
#ifndef FMT_LOG_Q
#define FMT_LOG_Q 8
#endif

static inline int32_t log2_q8(uint32_t v) {
    if (!v) return -2147483647L - 1L; // INT32_MIN
    int e = fast_msb32(v);
    uint8_t m;
    if (e >= 7) {
        m = (uint8_t)(v >> (e - 7));
    } else {
        m = (uint8_t)(v << (7 - e));
    }
    return ((int32_t)(e - 7) << FMT_LOG_Q) + FMT_READ16(log2_table_q8, m);
}

static inline uint32_t exp2_q8(int32_t y) {
    if (y == (-2147483647L - 1L)) return 0;
    int32_t ip = y >> FMT_LOG_Q;
    uint16_t fr = (uint16_t)(y & ((1 << FMT_LOG_Q) - 1));
    uint32_t v = FMT_READ16(exp2_table_q8, fr); // Q8 scaled
    if (ip >= 0) {
        if (ip > 31) return 0xFFFFFFFFUL;
        if (ip >= FMT_LOG_Q) return v << (ip - FMT_LOG_Q);
        return v >> (FMT_LOG_Q - ip);
    } else {
        int s = FMT_LOG_Q - ip;
        if (s >= 31) return 0;
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
