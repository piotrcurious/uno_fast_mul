#ifndef FMT_RING_H
#define FMT_RING_H

#include "FMT_Core.h"

namespace FMT {

/**
 * Log32 represents a number in the log2 domain: value = sign * 2^(lval / 2^FMT_LOG_Q)
 * This is a "Ring Extension" where we perform multiplications as additions.
 */
typedef struct {
    int16_t lval; // Logarithmic value in Q8.8
    int8_t sign;  // Sign: 1, -1, or 0
} Log32;

static inline Log32 to_log32(int32_t v) {
    Log32 l;
    if (v == 0) {
        l.lval = -32768;
        l.sign = 0;
    } else if (v > 0) {
        l.lval = (int16_t)log2_q8((uint32_t)v);
        l.sign = 1;
    } else {
        l.lval = (int16_t)log2_q8((uint32_t)-v);
        l.sign = -1;
    }
    return l;
}

static inline int32_t from_log32(const Log32 &l) {
    if (l.sign == 0) return 0;
    int32_t res = (int32_t)exp2_q8((int32_t)l.lval);
    return (l.sign > 0) ? res : -res;
}

static inline Log32 log32_mul(const Log32 &a, const Log32 &b) {
    Log32 r;
    r.sign = a.sign * b.sign;
    if (r.sign == 0) {
        r.lval = -32768;
    } else {
        r.lval = a.lval + b.lval;
    }
    return r;
}

static inline Log32 log32_div(const Log32 &a, const Log32 &b) {
    Log32 r;
    if (b.sign == 0) {
        r.sign = (a.sign >= 0) ? 1 : -1;
        r.lval = 32767;
        return r;
    }
    r.sign = a.sign * b.sign;
    if (a.sign == 0) {
        r.lval = -32768;
    } else {
        r.lval = a.lval - b.lval;
    }
    return r;
}

static inline Log32 log32_pow(const Log32 &a, float k) {
    Log32 r;
    if (a.sign == 0) {
        r.sign = 0;
        r.lval = -32768;
    } else {
        r.sign = 1;
        r.lval = (int16_t)(a.lval * k);
    }
    return r;
}

static inline Log32 log32_add(const Log32 &a, const Log32 &b) {
    if (a.sign == 0) return b;
    if (b.sign == 0) return a;

    if (a.sign == b.sign) {
        Log32 r;
        r.sign = a.sign;
        int16_t diff = a.lval - b.lval;
        if (diff >= 0) {
            uint16_t table_idx = (uint16_t)(diff >> 3);
            if (table_idx > 255) table_idx = 255;
            r.lval = a.lval + (int16_t)FMT_READ16(lse_table_q8, table_idx);
        } else {
            uint16_t table_idx = (uint16_t)((-diff) >> 3);
            if (table_idx > 255) table_idx = 255;
            r.lval = b.lval + (int16_t)FMT_READ16(lse_table_q8, table_idx);
        }
        return r;
    } else {
        return to_log32(from_log32(a) + from_log32(b));
    }
}

} // namespace FMT

#endif
