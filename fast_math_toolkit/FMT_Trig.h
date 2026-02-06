#ifndef FMT_TRIG_H
#define FMT_TRIG_H

#include "FMT_Core.h"

namespace FMT {

#ifndef FMT_SIN_Q
#define FMT_SIN_Q 15
#endif

// Angle: 0..65535 maps to 0..2*PI
static inline int16_t sin_u16(uint16_t a) {
#ifdef SIN_TABLE_Q15_SIZE
#if SIN_TABLE_Q15_SIZE == 1024
    uint16_t idx = a >> 6;
#elif SIN_TABLE_Q15_SIZE == 512
    uint16_t idx = a >> 7;
#elif SIN_TABLE_Q15_SIZE == 256
    uint16_t idx = a >> 8;
#else
    uint16_t idx = ((uint32_t)a * SIN_TABLE_Q15_SIZE) >> 16;
#endif
    if (idx >= SIN_TABLE_Q15_SIZE) idx = 0;
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
    uint16_t idx = a >> 6;
#elif COS_TABLE_Q15_SIZE == 512
    uint16_t idx = a >> 7;
#elif COS_TABLE_Q15_SIZE == 256
    uint16_t idx = a >> 8;
#else
    uint16_t idx = ((uint32_t)a * COS_TABLE_Q15_SIZE) >> 16;
#endif
    if (idx >= COS_TABLE_Q15_SIZE) idx = 0;
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

} // namespace FMT

#endif
