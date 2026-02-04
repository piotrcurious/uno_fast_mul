#include "fast_float.h"

#ifdef __AVR__
#include <avr/pgmspace.h>
static inline uint16_t read_word(const uint16_t *addr) {
    return pgm_read_word(addr);
}
static inline int16_t read_sword(const int16_t *addr) {
    return (int16_t)pgm_read_word(addr);
}
#else
static inline uint16_t read_word(const uint16_t *addr) {
    return *addr;
}
static inline int16_t read_sword(const int16_t *addr) {
    return *addr;
}
#endif

typedef union {
    float f;
    uint32_t u;
} float_conv;

static uint16_t btm_log2(uint32_t mantissa_bits) {
    uint16_t idx = (uint16_t)(mantissa_bits >> (23 - 14));
    uint16_t i12 = idx >> 5; // 9 bits
    uint16_t i13 = ((idx >> 10) << 5) | (idx & 31); // 9 bits

    uint16_t t1 = read_word(&log2_t1[i12]);
    int16_t t2 = read_sword(&log2_t2[i13]);

    int32_t res = (int32_t)t1 + t2;
    if (res < 0) return 0;
    if (res > 65535) return 65535;
    return (uint16_t)res;
}

static uint32_t btm_exp2(uint16_t log_frac) {
    uint16_t idx = log_frac >> (16 - 14);
    uint16_t i12 = idx >> 5;
    uint16_t i13 = ((idx >> 10) << 5) | (idx & 31);

    uint16_t t1 = read_word(&exp2_t1[i12]);
    int16_t t2 = read_sword(&exp2_t2[i13]);

    int32_t res = (int32_t)t1 + t2;
    if (res < 0) res = 0;
    if (res > 65535) res = 65535;

    uint32_t res_q16 = (uint32_t)res;
    return (res_q16 << 7); // Q16 to Q23
}

float fast_mul_f32(float a, float b) {
    float_conv ca, cb, cr;
    ca.f = a;
    cb.f = b;

    uint32_t a_abs = ca.u & 0x7FFFFFFF;
    uint32_t b_abs = cb.u & 0x7FFFFFFF;
    if (a_abs == 0 || b_abs == 0) return 0.0f;

    uint32_t sa = ca.u >> 31;
    uint32_t sb = cb.u >> 31;
    int32_t ea = (ca.u >> 23) & 0xFF;
    int32_t eb = (cb.u >> 23) & 0xFF;
    uint32_t ma = ca.u & 0x7FFFFF;
    uint32_t mb = cb.u & 0x7FFFFF;

    uint16_t la = btm_log2(ma);
    uint16_t lb = btm_log2(mb);

    uint32_t lsum = (uint32_t)la + lb;
    int32_t carry = (int32_t)(lsum >> 16);
    uint16_t lfrac = lsum & 0xFFFF;

    int32_t er = ea + eb - 127 + carry;
    uint32_t mr = btm_exp2(lfrac);
    uint32_t sr = sa ^ sb;

    if (er <= 0) return 0.0f;
    if (er >= 255) {
        cr.u = (sr << 31) | 0x7F800000;
        return cr.f;
    }

    cr.u = (sr << 31) | ((uint32_t)er << 23) | mr;
    return cr.f;
}

float fast_div_f32(float a, float b) {
    float_conv ca, cb, cr;
    ca.f = a;
    cb.f = b;

    uint32_t a_abs = ca.u & 0x7FFFFFFF;
    uint32_t b_abs = cb.u & 0x7FFFFFFF;
    if (a_abs == 0) return 0.0f;
    if (b_abs == 0) {
        cr.u = ((ca.u ^ cb.u) & 0x80000000) | 0x7F800000;
        return cr.f;
    }

    uint32_t sa = ca.u >> 31;
    uint32_t sb = cb.u >> 31;
    int32_t ea = (ca.u >> 23) & 0xFF;
    int32_t eb = (cb.u >> 23) & 0xFF;
    uint32_t ma = ca.u & 0x7FFFFF;
    uint32_t mb = cb.u & 0x7FFFFF;

    uint16_t la = btm_log2(ma);
    uint16_t lb = btm_log2(mb);

    int32_t ldiff = (int32_t)la - lb;
    int32_t carry = 0;
    if (ldiff < 0) {
        ldiff += 65536;
        carry = -1;
    }
    uint16_t lfrac = (uint16_t)ldiff;

    int32_t er = ea - eb + 127 + carry;
    uint32_t mr = btm_exp2(lfrac);
    uint32_t sr = sa ^ sb;

    if (er <= 0) return 0.0f;
    if (er >= 255) {
        cr.u = (sr << 31) | 0x7F800000;
        return cr.f;
    }

    cr.u = (sr << 31) | ((uint32_t)er << 23) | mr;
    return cr.f;
}
