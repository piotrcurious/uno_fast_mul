#ifndef FMT_FIXED_H
#define FMT_FIXED_H

#include "FMT_Core.h"

namespace FMT {

enum {
    Q16_S = 16
};
#define Q16_ONE (1L << Q16_S)

// Q16.16 Exact
// We let the compiler handle 64-bit intermediate products as it is highly optimized on AVR.
static inline uint32_t q16_mul_u(uint32_t a, uint32_t b) { return (uint32_t)(((uint64_t)a * b) >> Q16_S); }
static inline int32_t  q16_mul_s(int32_t a, int32_t b)  { return (int32_t)(((int64_t)a * b) >> Q16_S); }

static inline uint32_t q16_div_u(uint32_t a, uint32_t b) {
    if (!b) return 0xFFFFFFFFUL;
    return (uint32_t)(((uint64_t)a << Q16_S) / b);
}
static inline int32_t  q16_div_s(int32_t a, int32_t b)  {
    if (!b) return (a >= 0) ? 0x7FFFFFFF : -0x7FFFFFFF - 1;
    return (int32_t)(((int64_t)a << Q16_S) / b);
}

// Q16.16 Approximate (faster for chained operations or special platforms)
static inline int32_t q16_div_s_ap(int32_t a, int32_t b) {
    bool n = (a < 0) ^ (b < 0);
    uint32_t ua = (a < 0) ? -(uint32_t)a : (uint32_t)a;
    uint32_t ub = (b < 0) ? -(uint32_t)b : (uint32_t)b;
    if (!ub) return n ? -2147483647L - 1L : 2147483647L;
    int32_t res = (int32_t)exp2_q8(log2_q8(ua) - log2_q8(ub) + (16L << FMT_LOG_Q));
    return n ? -res : res;
}

static inline uint32_t q16_mul_u_ap(uint32_t a, uint32_t b) {
    if (!a || !b) return 0;
    return exp2_q8(log2_q8(a) + log2_q8(b) - (16 << FMT_LOG_Q));
}

// Float conversion
static inline int32_t  q16_from_float(float f) { return (int32_t)(f * 65536.0f); }
static inline float    q16_to_float(int32_t q)   { return (float)q / 65536.0f; }

static inline uint32_t q16_inv_sqrt(uint32_t x) {
    if (!x) return 0xFFFFFFFFUL;
    int32_t lx = log2_q8(x);
    return exp2_q8((24L << FMT_LOG_Q) - (lx >> 1));
}

static inline uint32_t q16_sqrt(uint32_t x) {
    if (!x) return 0;
    int32_t lx = log2_q8(x);
    return exp2_q8((lx >> 1) + (8L << FMT_LOG_Q));
}

static inline int32_t q16_lerp(int32_t a, int32_t b, int32_t t) {
    return a + (int32_t)(((int64_t)(b - a) * t) >> Q16_S);
}

} // namespace FMT

#endif
