#ifndef FMT_RING_H
#define FMT_RING_H

#include "FMT_Core.h"

namespace FMT {

/**
 * Log32 represents a number in the log2 domain: value = sign * 2^(lval / 2^FMT_LOG_Q)
 * This is a "Ring Extension" where we perform multiplications as additions.
 */
typedef struct {
    int32_t lval; // Logarithmic value in Q8.8
    int8_t sign;  // Sign: 1, -1, or 0
} Log32;

static inline Log32 to_log32(int32_t v) {
    Log32 l;
    if (v == 0) {
        l.lval = -2147483647L - 1L;
        l.sign = 0;
    } else if (v > 0) {
        l.lval = log2_q8((uint32_t)v);
        l.sign = 1;
    } else {
        l.lval = log2_q8((uint32_t)-v);
        l.sign = -1;
    }
    return l;
}

static inline int32_t from_log32(Log32 l) {
    if (l.sign == 0) return 0;
    int32_t res = (int32_t)exp2_q8(l.lval);
    return (l.sign > 0) ? res : -res;
}

static inline Log32 log32_mul(Log32 a, Log32 b) {
    Log32 r;
    r.sign = a.sign * b.sign;
    if (r.sign == 0) {
        r.lval = -2147483647L - 1L;
    } else {
        r.lval = a.lval + b.lval;
    }
    return r;
}

static inline Log32 log32_div(Log32 a, Log32 b) {
    Log32 r;
    if (b.sign == 0) {
        r.sign = (a.sign >= 0) ? 1 : -1; // Infinity-ish
        r.lval = 0x7FFFFFFF;
        return r;
    }
    r.sign = a.sign * b.sign;
    if (a.sign == 0) {
        r.lval = -2147483647L - 1L;
    } else {
        r.lval = a.lval - b.lval;
    }
    return r;
}

static inline Log32 log32_pow(Log32 a, float k) {
    Log32 r;
    if (a.sign == 0) {
        r.sign = 0;
        r.lval = -2147483647L - 1L;
    } else {
        r.sign = 1; // Power of negative is complex, we assume positive or absolute
        r.lval = (int32_t)(a.lval * k);
    }
    return r;
}

} // namespace FMT

#endif
