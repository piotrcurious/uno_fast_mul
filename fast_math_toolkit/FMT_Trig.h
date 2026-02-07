#ifndef FMT_TRIG_H
#define FMT_TRIG_H

#include "FMT_Core.h"
#include "FMT_Ring.h"

namespace FMT {

#ifndef FMT_SIN_Q
#define FMT_SIN_Q 15
#endif

// Angle: 0..65535 maps to 0..2*PI
static inline int16_t sin_u16(uint16_t a) {
#ifdef SIN_TABLE_Q15_SIZE
#if SIN_TABLE_Q15_SIZE == 1024
    uint16_t idx = (a >> 6) & 1023;
#elif SIN_TABLE_Q15_SIZE == 512
    uint16_t idx = (a >> 7) & 511;
#elif SIN_TABLE_Q15_SIZE == 256
    uint16_t idx = (a >> 8) & 255;
#else
    uint16_t idx = ((uint32_t)a * SIN_TABLE_Q15_SIZE) >> 16;
    if (idx >= SIN_TABLE_Q15_SIZE) idx = 0;
#endif
#else
    uint32_t n = sizeof(sin_table_q15) / 2;
    uint32_t idx = ((uint32_t)a * n) >> 16;
    if (idx >= n) idx = 0;
#endif
    return FMT_READ_S16(sin_table_q15, (uint16_t)idx);
}

static inline int16_t cos_u16(uint16_t a) {
#ifdef COS_TABLE_Q15_SIZE
#if COS_TABLE_Q15_SIZE == 1024
    uint16_t idx = (a >> 6) & 1023;
#elif COS_TABLE_Q15_SIZE == 512
    uint16_t idx = (a >> 7) & 511;
#elif COS_TABLE_Q15_SIZE == 256
    uint16_t idx = (a >> 8) & 255;
#else
    uint16_t idx = ((uint32_t)a * COS_TABLE_Q15_SIZE) >> 16;
    if (idx >= COS_TABLE_Q15_SIZE) idx = 0;
#endif
#else
    uint32_t n = sizeof(cos_table_q15) / 2;
    uint32_t idx = ((uint32_t)a * n) >> 16;
    if (idx >= n) idx = 0;
#endif
    return FMT_READ_S16(cos_table_q15, (uint16_t)idx);
}

// Q16.16 versions
static inline int32_t sin_q16(uint16_t a) { return (int32_t)sin_u16(a) << 1; }
static inline int32_t cos_q16(uint16_t a) { return (int32_t)cos_u16(a) << 1; }

static inline Log32 sin_log(uint16_t a) {
#ifdef SIN_TABLE_Q15_SIZE
    uint16_t idx;
#if SIN_TABLE_Q15_SIZE == 1024
    idx = (a >> 6) & 1023;
#elif SIN_TABLE_Q15_SIZE == 512
    idx = (a >> 7) & 511;
#elif SIN_TABLE_Q15_SIZE == 256
    idx = (a >> 8) & 255;
#else
    idx = ((uint32_t)a * SIN_TABLE_Q15_SIZE) >> 16;
    if (idx >= SIN_TABLE_Q15_SIZE) idx = 0;
#endif
    Log32 r;
    int16_t s = FMT_READ_S16(sin_table_q15, idx);
    r.sign = (s > 0) ? 1 : (s < 0 ? -1 : 0);
    r.lval = (int16_t)FMT_READ16(log_sin_table_q8, idx);
    return r;
#else
    // Fallback to calculation
    int16_t s = sin_u16(a);
    return to_log32((int32_t)s << 1); // sin_u16 is Q15, we want Q16.16 log domain
#endif
}

static inline Log32 cos_log(uint16_t a) {
#ifdef COS_TABLE_Q15_SIZE
    uint16_t idx;
#if COS_TABLE_Q15_SIZE == 1024
    idx = (a >> 6) & 1023;
#elif COS_TABLE_Q15_SIZE == 512
    idx = (a >> 7) & 511;
#elif COS_TABLE_Q15_SIZE == 256
    idx = (a >> 8) & 255;
#else
    idx = ((uint32_t)a * COS_TABLE_Q15_SIZE) >> 16;
    if (idx >= COS_TABLE_Q15_SIZE) idx = 0;
#endif
    Log32 r;
    int16_t c = FMT_READ_S16(cos_table_q15, idx);
    r.sign = (c > 0) ? 1 : (c < 0 ? -1 : 0);
    r.lval = (int16_t)FMT_READ16(log_cos_table_q8, idx);
    return r;
#else
    int16_t c = cos_u16(a);
    return to_log32((int32_t)c << 1);
#endif
}

static inline uint16_t atan2_u16(int32_t y, int32_t x) {
    if (x == 0 && y == 0) return 0;

    uint32_t ux = (x < 0) ? -x : x;
    uint32_t uy = (y < 0) ? -y : y;

    uint16_t angle;
    if (uy <= ux) {
        // Octant 0, 3, 4, 7
        // z = uy/ux in [0, 1]. Map to 0..255
        uint16_t idx = (uy == 0) ? 0 : (uint16_t)(((uint64_t)uy * 255) / ux);
        angle = FMT_READ16(atan_q15_table, idx);
    } else {
        // Octant 1, 2, 5, 6
        uint16_t idx = (ux == 0) ? 0 : (uint16_t)(((uint64_t)ux * 255) / uy);
        angle = 16384 - FMT_READ16(atan_q15_table, idx);
    }

    if (x < 0) {
        if (y >= 0) angle = 32768 - angle;
        else angle = 32768 + angle;
    } else {
        if (y < 0) angle = 65536 - angle;
    }

    return angle;
}

static inline uint16_t acos_u16(int32_t x) {
    uint32_t ux = (x < 0) ? -x : x;
    if (ux > Q16_ONE) ux = Q16_ONE;

    // x in [0, 1] mapped to 0..255
    uint16_t idx = (ux >> 8);
    if (idx > 255) idx = 255;

    uint16_t angle = FMT_READ16(acos_table, idx);

    if (x < 0) {
        // acos(-x) = PI - acos(x)
        angle = 32768 - angle;
    }
    return angle;
}

} // namespace FMT

#endif
