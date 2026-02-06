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

static inline Log32 log32_add(Log32 a, Log32 b) {
    if (a.sign == 0) return b;
    if (b.sign == 0) return a;

    if (a.sign == b.sign) {
        // LogSumExp: log(2^a + 2^b) = max(a,b) + log2(1 + 2^-|a-b|)
        Log32 r;
        r.sign = a.sign;
        int32_t diff = a.lval - b.lval;
        if (diff >= 0) {
            // a is larger or equal
            // diff is 0..inf.
            // We use table for log2(1 + 2^-diff)
            // diff is Q8.8. Table covers 0..8.0
            // Wait, if table size 256 covers 0..8, then idx = diff / (8/256) = diff * 32.
            // But if table size 256 covers 0..8, then each entry is 8/256 = 1/32 log units.
            // My gen_lse_table(256, q=8, x_range=8.0)
            // idx = (x / x_range) * (n-1) = (diff/256 / 8) * 255 = diff * 255 / 2048 approx.
            // Let's use simpler indexing: idx = diff >> 3 (since 2^3=8) if we want 1/256 resolution? No.
            // If x_range is 8.0, and n is 256, then idx = diff >> 3. (because diff is Q8.8, diff>>8 is integer, diff>>3 is integer*32).
            uint16_t table_idx = (diff >> 3);
            if (table_idx > 255) table_idx = 255;
            r.lval = a.lval + FMT_READ16(lse_table_q8, table_idx);
        } else {
            diff = -diff;
            uint16_t table_idx = (diff >> 3);
            if (table_idx > 255) table_idx = 255;
            r.lval = b.lval + FMT_READ16(lse_table_q8, table_idx);
        }
        return r;
    } else {
        // LogSubExp: log(2^a - 2^b). Tricky.
        // For simplicity, we convert to linear or ignore for now.
        // Usually 3D math has more additions of positive terms (like dot products of squares).
        return to_log32(from_log32(a) + from_log32(b));
    }
}

} // namespace FMT

#endif
