/* * FastMathToolkit.h
 * * Arduino-friendly single-header implementing fast fixed-point 3D operations.
 * Optimized for Q16.16 fixed-point math with 64-bit intermediate precision.
 * * Requirements:
 * - A generated "arduino_tables_generated.h" defining:
 * sin_table_q15, cos_table_q15, perspective_scale_table_q8
 */

#ifndef FAST_MATH_TOOLKIT_H
#define FAST_MATH_TOOLKIT_H

#include <stdint.h>
#include <limits.h>

#ifdef ARDUINO
#include <avr/pgmspace.h>
#else
#define PROGMEM
#endif

#ifndef INCLUDE_TABLES
#define INCLUDE_TABLES "arduino_tables_generated.h"
#endif
#include INCLUDE_TABLES

namespace FastMath {

// Configuration
#ifndef FASTMATH_LOG_Q
#define FASTMATH_LOG_Q 8
#endif
#ifndef FASTMATH_SIN_Q
#define FASTMATH_SIN_Q 15
#endif

#define Q16_SHIFT 16
#define Q16_ONE   (1L << Q16_SHIFT)

// --- Types ---

typedef struct {
    int32_t x, y, z;
} Vec3_q16;

typedef struct {
    int32_t m[3][3];
} Mat3_q16;

typedef struct {
    int32_t w, x, y, z;
} Quat_q16;

// --- Read Helpers ---

static inline uint8_t read_u8_prog(const uint8_t *arr, uint16_t idx) {
#ifdef ARDUINO
    return pgm_read_byte(&arr[idx]);
#else
    return arr[idx];
#endif
}

static inline uint16_t read_u16_prog(const uint16_t *arr, uint16_t idx) {
#ifdef ARDUINO
    return pgm_read_word(&arr[idx]);
#else
    return arr[idx];
#endif
}

static inline int16_t read_s16_prog(const int16_t *arr, uint16_t idx) {
#ifdef ARDUINO
    return (int16_t)pgm_read_word(&arr[idx]);
#else
    return arr[idx];
#endif
}

// --- Lookup Table Accessors ---

static inline int16_t sin_q15_from_u16angle(uint16_t angle) {
    // Assumes table size 256 or similar; adjust index mask based on your generator
    return read_s16_prog(sin_table_q15, angle >> 8); 
}

static inline int16_t cos_q15_from_u16angle(uint16_t angle) {
    return read_s16_prog(cos_table_q15, angle >> 8);
}

// --- Q16 Arithmetic ---

static inline int32_t q16_from_q15(int16_t v_q15) {
    return ((int32_t)v_q15) << 1;
}

static inline int32_t q16_mul(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * b) >> Q16_SHIFT);
}

static inline int32_t q16_div(int32_t a, int32_t b) {
    if (b == 0) return (a >= 0) ? INT32_MAX : INT32_MIN;
    return (int32_t)(((int64_t)a << Q16_SHIFT) / b);
}

// --- Vector Operations ---

static inline Vec3_q16 vec3_init(int32_t x, int32_t y, int32_t z) {
    Vec3_q16 v = {x, y, z};
    return v;
}

static inline Vec3_q16 vec3_add(const Vec3_q16 &a, const Vec3_q16 &b) {
    return vec3_init(a.x + b.x, a.y + b.y, a.z + b.z);
}

static inline Vec3_q16 vec3_sub(const Vec3_q16 &a, const Vec3_q16 &b) {
    return vec3_init(a.x - b.x, a.y - b.y, a.z - b.z);
}

// --- 3D Transforms ---

static inline Vec3_q16 mat3_mul_vec(const Mat3_q16 &M, const Vec3_q16 &v) {
    Vec3_q16 r;
    r.x = q16_mul(M.m[0][0], v.x) + q16_mul(M.m[0][1], v.y) + q16_mul(M.m[0][2], v.z);
    r.y = q16_mul(M.m[1][0], v.x) + q16_mul(M.m[1][1], v.y) + q16_mul(M.m[1][2], v.z);
    r.z = q16_mul(M.m[2][0], v.x) + q16_mul(M.m[2][1], v.y) + q16_mul(M.m[2][2], v.z);
    return r;
}

static inline Mat3_q16 mat3_rotation_euler(uint16_t ax, uint16_t ay, uint16_t az) {
    int32_t SX = q16_from_q15(sin_q15_from_u16angle(ax));
    int32_t CX = q16_from_q15(cos_q15_from_u16angle(ax));
    int32_t SY = q16_from_q15(sin_q15_from_u16angle(ay));
    int32_t CY = q16_from_q15(cos_q15_from_u16angle(ay));
    int32_t SZ = q16_from_q15(sin_q15_from_u16angle(az));
    int32_t CZ = q16_from_q15(cos_q15_from_u16angle(az));

    Mat3_q16 M;
    // Order: ZYX
    M.m[0][0] = q16_mul(CZ, CY);
    M.m[0][1] = q16_mul(q16_mul(CZ, SY), SX) - q16_mul(SZ, CX);
    M.m[0][2] = q16_mul(q16_mul(CZ, SY), CX) + q16_mul(SZ, SX);

    M.m[1][0] = q16_mul(SZ, CY);
    M.m[1][1] = q16_mul(q16_mul(SZ, SY), SX) + q16_mul(CZ, CX);
    M.m[1][2] = q16_mul(q16_mul(SZ, SY), CX) - q16_mul(CZ, SX);

    M.m[2][0] = -SY;
    M.m[2][1] = q16_mul(CY, SX);
    M.m[2][2] = q16_mul(CY, CX);
    return M;
}

static inline Vec3_q16 vec3_rotate_x(const Vec3_q16 &v, uint16_t angle) {
    int32_t S = q16_from_q15(sin_q15_from_u16angle(angle));
    int32_t C = q16_from_q15(cos_q15_from_u16angle(angle));
    return vec3_init(v.x, q16_mul(v.y, C) - q16_mul(v.z, S), q16_mul(v.y, S) + q16_mul(v.z, C));
}

static inline Vec3_q16 vec3_rotate_y(const Vec3_q16 &v, uint16_t angle) {
    int32_t S = q16_from_q15(sin_q15_from_u16angle(angle));
    int32_t C = q16_from_q15(cos_q15_from_u16angle(angle));
    return vec3_init(q16_mul(v.x, C) + q16_mul(v.z, S), v.y, -q16_mul(v.x, S) + q16_mul(v.z, C));
}

static inline Vec3_q16 vec3_rotate_z(const Vec3_q16 &v, uint16_t angle) {
    int32_t S = q16_from_q15(sin_q15_from_u16angle(angle));
    int32_t C = q16_from_q15(cos_q15_from_u16angle(angle));
    return vec3_init(q16_mul(v.x, C) - q16_mul(v.y, S), q16_mul(v.x, S) + q16_mul(v.y, C), v.z);
}

// --- Quaternions ---

static inline Quat_q16 quat_from_axis_angle(int32_t ax, int32_t ay, int32_t az, uint16_t angle) {
    int32_t s = q16_from_q15(sin_q15_from_u16angle(angle >> 1));
    int32_t c = q16_from_q15(cos_q15_from_u16angle(angle >> 1));
    Quat_q16 q = {c, q16_mul(ax, s), q16_mul(ay, s), q16_mul(az, s)};
    return q;
}

static inline Vec3_q16 quat_rotate_vec(const Quat_q16 &q, const Vec3_q16 &v) {
    // Optimized formula: v' = v + 2*q_vec x (q_vec x v + q.w * v)
    int32_t tx = 2 * (q16_mul(q.y, v.z) - q16_mul(q.z, v.y));
    int32_t ty = 2 * (q16_mul(q.z, v.x) - q16_mul(q.x, v.z));
    int32_t tz = 2 * (q16_mul(q.x, v.y) - q16_mul(q.y, v.x));

    int32_t res_x = v.x + q16_mul(q.w, tx) + (q16_mul(q.y, tz) - q16_mul(q.z, ty));
    int32_t res_y = v.y + q16_mul(q.w, ty) + (q16_mul(q.z, tx) - q16_mul(q.x, tz));
    int32_t res_z = v.z + q16_mul(q.w, tz) + (q16_mul(q.x, ty) - q16_mul(q.y, tx));
    return vec3_init(res_x, res_y, res_z);
}

// --- Projection ---

static inline Vec3_q16 project_perspective(const Vec3_q16 &v, int32_t focal_q16) {
    int32_t denom = v.z + focal_q16;
    if (denom == 0) denom = 1;
    return vec3_init(q16_div(q16_mul(v.x, focal_q16), denom), 
                     q16_div(q16_mul(v.y, focal_q16), denom), 
                     v.z);
}

static inline Vec3_q16 pipeline_mvp(const Vec3_q16 &v_local, int32_t scale_q16, 
                                   uint16_t ax, uint16_t ay, uint16_t az, 
                                   const Vec3_q16 &trans, int32_t focal_q16) {
    Mat3_q16 R = mat3_rotation_euler(ax, ay, az);
    
    // Scale + Rotate
    Vec3_q16 world = vec3_init(q16_mul(v_local.x, scale_q16), 
                               q16_mul(v_local.y, scale_q16), 
                               q16_mul(v_local.z, scale_q16));
    world = mat3_mul_vec(R, world);
    
    // Translate
    world.x += trans.x;
    world.y += trans.y;
    world.z += trans.z;
    
    return project_perspective(world, focal_q16);
}

} // namespace FastMath

// --- C Exports ---
#ifdef __cplusplus
extern "C" {
#endif

typedef FastMath::Vec3_q16 FM_Vec3;

static inline FM_Vec3 FM_RotateY(FM_Vec3 v, uint16_t ang) {
    return FastMath::vec3_rotate_y(v, ang);
}

static inline FM_Vec3 FM_Project(FM_Vec3 v, int32_t focal) {
    return FastMath::project_perspective(v, focal);
}

#ifdef __cplusplus
}
#endif

#endif // FAST_MATH_TOOLKIT_H

