#ifndef FMT_3D_H
#define FMT_3D_H

#include "FMT_Fixed.h"
#include "FMT_Trig.h"

namespace FMT {

typedef struct {
    int32_t x, y, z;
} Vec3;

typedef struct {
    int32_t m[3][3];
} Mat3;

typedef struct {
    int32_t w, x, y, z;
} Quat;

static inline Vec3 vec3_init(int32_t x, int32_t y, int32_t z) {
    Vec3 v = {x, y, z};
    return v;
}

static inline Vec3 vec3_add(Vec3 a, Vec3 b) {
    return vec3_init(a.x + b.x, a.y + b.y, a.z + b.z);
}

static inline Vec3 vec3_sub(Vec3 a, Vec3 b) {
    return vec3_init(a.x - b.x, a.y - b.y, a.z - b.z);
}

static inline int32_t vec3_dot(Vec3 a, Vec3 b) {
    return q16_mul_s(a.x, b.x) + q16_mul_s(a.y, b.y) + q16_mul_s(a.z, b.z);
}

static inline Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return vec3_init(q16_mul_s(a.y, b.z) - q16_mul_s(a.z, b.y),
                     q16_mul_s(a.z, b.x) - q16_mul_s(a.x, b.z),
                     q16_mul_s(a.x, b.y) - q16_mul_s(a.y, b.x));
}

static inline Vec3 vec3_normalize(Vec3 v) {
    int32_t dot = vec3_dot(v, v);
    if (dot <= 0) return v;
    uint32_t isqr = q16_inv_sqrt((uint32_t)dot);
    return vec3_init(q16_mul_s(v.x, isqr), q16_mul_s(v.y, isqr), q16_mul_s(v.z, isqr));
}

static inline Vec3 mat3_mul_vec(const Mat3 *M, Vec3 v) {
    Vec3 r;
    r.x = q16_mul_s(M->m[0][0], v.x) + q16_mul_s(M->m[0][1], v.y) + q16_mul_s(M->m[0][2], v.z);
    r.y = q16_mul_s(M->m[1][0], v.x) + q16_mul_s(M->m[1][1], v.y) + q16_mul_s(M->m[1][2], v.z);
    r.z = q16_mul_s(M->m[2][0], v.x) + q16_mul_s(M->m[2][1], v.y) + q16_mul_s(M->m[2][2], v.z);
    return r;
}

static inline Mat3 mat3_rotation_euler(uint16_t ax, uint16_t ay, uint16_t az) {
    int32_t sx = sin_q16(ax), cx = cos_q16(ax);
    int32_t sy = sin_q16(ay), cy = cos_q16(ay);
    int32_t sz = sin_q16(az), cz = cos_q16(az);

    Mat3 M;
    // Order: ZYX
    M.m[0][0] = q16_mul_s(cz, cy);
    M.m[0][1] = q16_mul_s(q16_mul_s(cz, sy), sx) - q16_mul_s(sz, cx);
    M.m[0][2] = q16_mul_s(q16_mul_s(cz, sy), cx) + q16_mul_s(sz, sx);

    M.m[1][0] = q16_mul_s(sz, cy);
    M.m[1][1] = q16_mul_s(q16_mul_s(sz, sy), sx) + q16_mul_s(cz, cx);
    M.m[1][2] = q16_mul_s(q16_mul_s(sz, sy), cx) - q16_mul_s(cz, sx);

    M.m[2][0] = -sy;
    M.m[2][1] = q16_mul_s(cy, sx);
    M.m[2][2] = q16_mul_s(cy, cx);
    return M;
}

static inline Vec3 project_perspective(Vec3 v, int32_t focal) {
    int32_t denom = v.z + focal;
    if (denom == 0) denom = 1;
    return vec3_init(q16_div_s(q16_mul_s(v.x, focal), denom),
                     q16_div_s(q16_mul_s(v.y, focal), denom),
                     v.z);
}

static inline Quat quat_from_axis_angle(int32_t ax, int32_t ay, int32_t az, uint16_t angle) {
    int32_t s = sin_q16(angle >> 1);
    int32_t c = cos_q16(angle >> 1);
    Quat q = {c, q16_mul_s(ax, s), q16_mul_s(ay, s), q16_mul_s(az, s)};
    return q;
}

static inline Vec3 quat_rotate_vec(Quat q, Vec3 v) {
    // v' = v + 2*q_vec x (q_vec x v + q.w * v)
    int32_t tx = 2 * (q16_mul_s(q.y, v.z) - q16_mul_s(q.z, v.y));
    int32_t ty = 2 * (q16_mul_s(q.z, v.x) - q16_mul_s(q.x, v.z));
    int32_t tz = 2 * (q16_mul_s(q.x, v.y) - q16_mul_s(q.y, v.x));

    int32_t res_x = v.x + q16_mul_s(q.w, tx) + (q16_mul_s(q.y, tz) - q16_mul_s(q.z, ty));
    int32_t res_y = v.y + q16_mul_s(q.w, ty) + (q16_mul_s(q.z, tx) - q16_mul_s(q.x, tz));
    int32_t res_z = v.z + q16_mul_s(q.w, tz) + (q16_mul_s(q.x, ty) - q16_mul_s(q.y, tx));
    return vec3_init(res_x, res_y, res_z);
}

static inline Vec3 pipeline_mvp(Vec3 v_local, int32_t scale,
                                uint16_t ax, uint16_t ay, uint16_t az,
                                Vec3 trans, int32_t focal) {
    Mat3 R = mat3_rotation_euler(ax, ay, az);
    Vec3 world = vec3_init(q16_mul_s(v_local.x, scale),
                           q16_mul_s(v_local.y, scale),
                           q16_mul_s(v_local.z, scale));
    world = mat3_mul_vec(&R, world);
    world.x += trans.x;
    world.y += trans.y;
    world.z += trans.z;
    return project_perspective(world, focal);
}

static inline Vec3 pipeline_mvp_fused(Vec3 v_local, int32_t scale,
                                      uint16_t ax, uint16_t ay, uint16_t az,
                                      Vec3 trans, int32_t focal) {
    Mat3 R = mat3_rotation_euler(ax, ay, az);

    // Scale is combined with rotation in linear space here, but let's see.
    // Actually, we can't easily fuse scale and rotation because rotation is additive.
    // But we CAN fuse Scale and focal/denom for the final projection if we are careful.

    Vec3 world = vec3_init(q16_mul_s(v_local.x, scale),
                           q16_mul_s(v_local.y, scale),
                           q16_mul_s(v_local.z, scale));
    world = mat3_mul_vec(&R, world);
    world.x += trans.x;
    world.y += trans.y;
    world.z += trans.z;

    // Final projection: x' = world.x * focal / (world.z + focal)
    int32_t denom = world.z + focal;
    if (denom <= 0) denom = 1;

    int32_t log_focal = log2_q8((uint32_t)focal);
    int32_t log_denom = log2_q8((uint32_t)denom);
    int32_t log_factor = log_focal - log_denom;

    Vec3 res;
    res.x = (world.x == 0) ? 0 : (int32_t)exp2_q8(log2_q8((uint32_t)(world.x > 0 ? world.x : -world.x)) + log_factor);
    if (world.x < 0) res.x = -res.x;

    res.y = (world.y == 0) ? 0 : (int32_t)exp2_q8(log2_q8((uint32_t)(world.y > 0 ? world.y : -world.y)) + log_factor);
    if (world.y < 0) res.y = -res.y;

    res.z = world.z;
    return res;
}

} // namespace FMT

#endif
