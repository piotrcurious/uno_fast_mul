/* FastMathToolkit.h - MIT License - Fast fixed-point math via LUTs */
#ifndef FAST_MATH_TOOLKIT_H
#define FAST_MATH_TOOLKIT_H

#include <stdint.h>
#include <limits.h>

#ifdef ARDUINO
#include <avr/pgmspace.h>
#define FM_READ8(a, i) pgm_read_byte(&(a)[i])
#define FM_READ16(a, i) pgm_read_word(&(a)[i])
#else
#define FM_READ8(a, i) (a)[i]
#define FM_READ16(a, i) (a)[i]
#endif

#ifndef INCLUDE_TABLES
#define INCLUDE_TABLES "arduino_tables_generated.h"
#endif
#include INCLUDE_TABLES

namespace FastMath {

#ifndef FASTMATH_LOG_Q
#define FASTMATH_LOG_Q 8
#endif

static inline int fast_msb32(uint32_t v) {
    if (!v) return -1;
    uint8_t t;
    if (v & 0xFFFF0000u) {
        uint16_t hi = (uint16_t)(v >> 16);
        if ((t = (uint8_t)(hi >> 8))) return 24 + FM_READ8(msb_table, t);
        return 16 + FM_READ8(msb_table, (uint8_t)hi);
    }
    if ((t = (uint8_t)(v >> 8))) return 8 + FM_READ8(msb_table, t);
    return FM_READ8(msb_table, (uint8_t)v);
}

static inline void normalize_to_mant8(uint32_t v, int *e, uint8_t *m) {
    if (!v) { *e = 0; *m = 0; return; }
    *e = fast_msb32(v);
    int s = *e - 7;
    *m = (s >= 0) ? (uint8_t)(v >> s) : (uint8_t)(v << -s);
    if (!*m) *m = 128;
}

static inline int32_t fast_log2_q8_u32(uint32_t v) {
    if (!v) return INT32_MIN;
    int e; uint8_t m;
    normalize_to_mant8(v, &e, &m);
    return ((int32_t)(e - 7) << FASTMATH_LOG_Q) + (int32_t)FM_READ16(log2_table_q8, m);
}

static inline uint32_t fast_exp2_q8_to_u32(int32_t y) {
    if (y == INT32_MIN) return 0;
    int32_t ip = y >> FASTMATH_LOG_Q;
    uint16_t f = (uint16_t)(y & ((1 << FASTMATH_LOG_Q) - 1));
    uint32_t fv = FM_READ16(exp2_table_q8, f);
    if (ip >= 0) return (ip > 24) ? UINT32_MAX : (fv << ip) >> FASTMATH_LOG_Q;
    int ds = FASTMATH_LOG_Q - ip;
    return (ds >= 31) ? 0 : (fv >> ds);
}

static inline uint32_t mul_u16_approx(uint16_t a, uint16_t b) {
    if (!a || !b) return 0;
    return fast_exp2_q8_to_u32(fast_log2_q8_u32(a) + fast_log2_q8_u32(b));
}

static inline uint32_t div_u32_by_u16_approx(uint32_t n, uint16_t d) {
    if (!d) return UINT32_MAX;
    if (!n) return 0;
    return fast_exp2_q8_to_u32(fast_log2_q8_u32(n) - fast_log2_q8_u32(d));
}

static inline uint32_t pow_u16_int_approx(uint16_t a, int k) {
    if (!a) return 0; if (!k) return 1;
    int32_t la = fast_log2_q8_u32(a);
    return (la == INT32_MIN) ? 0 : fast_exp2_q8_to_u32(la * k);
}

#define FM_TRIG_FUNC(name, table) \
static inline int16_t name(uint16_t a) { \
    const uint32_t N = sizeof(table)/sizeof(table[0]); \
    uint32_t i = ((uint32_t)a * N) >> 16; \
    return (int16_t)FM_READ16(table, (uint16_t)(i >= N ? 0 : i)); \
}

FM_TRIG_FUNC(sin_q15_from_u16angle, sin_table_q15)
FM_TRIG_FUNC(cos_q15_from_u16angle, cos_table_q15)

static inline uint32_t perspective_scale_from_index(uint16_t i) {
    const uint16_t N = sizeof(perspective_scale_table_q8)/sizeof(perspective_scale_table_q8[0]);
    return FM_READ16(perspective_scale_table_q8, (i >= N ? N - 1 : i));
}

static inline uint16_t stereographic_from_index(uint16_t i) {
    const uint16_t N = sizeof(stereo_radial_table_q12)/sizeof(stereo_radial_table_q12[0]);
    return FM_READ16(stereo_radial_table_q12, (i >= N ? N - 1 : i));
}

static inline uint32_t mul_u32_approx(uint32_t a, uint32_t b) {
    if (!a || !b) return 0;
    int sa = fast_msb32(a), sb = fast_msb32(b);
    int s = 0;
    if (sa > 15) s = sa - 15;
    if (sb > 15 && (sb - 15) > s) s = sb - 15;
    if (s >= 16) return 0;
    return mul_u16_approx(a >> s, b >> s) << (2 * s);
}

} // namespace FastMath

#ifdef __cplusplus
extern "C" {
#endif
static inline uint32_t FastMath_mul_u16_approx(uint16_t a, uint16_t b) { return FastMath::mul_u16_approx(a,b); }
static inline uint32_t FastMath_div_u32_by_u16_approx(uint32_t n, uint16_t d) { return FastMath::div_u32_by_u16_approx(n,d); }
static inline uint32_t FastMath_pow_u16_int_approx(uint16_t a, int k) { return FastMath::pow_u16_int_approx(a,k); }
static inline int16_t FastMath_sin_q15_from_u16angle(uint16_t a) { return FastMath::sin_q15_from_u16angle(a); }
static inline int16_t FastMath_cos_q15_from_u16angle(uint16_t a) { return FastMath::cos_q15_from_u16angle(a); }
#ifdef __cplusplus
}
#endif

#endif

