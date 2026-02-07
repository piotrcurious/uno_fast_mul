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

typedef struct {
    int32_t m[4][4];
} Mat4;

typedef struct {
    int32_t x, y, z, w;
} Vec4;

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
    return (int32_t)(((int64_t)a.x * b.x + (int64_t)a.y * b.y + (int64_t)a.z * b.z) >> Q16_S);
}

static inline Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return vec3_init((int32_t)(((int64_t)a.y * b.z - (int64_t)a.z * b.y) >> Q16_S),
                     (int32_t)(((int64_t)a.z * b.x - (int64_t)a.x * b.z) >> Q16_S),
                     (int32_t)(((int64_t)a.x * b.y - (int64_t)a.y * b.x) >> Q16_S));
}

static inline Vec3 vec3_normalize(Vec3 v) {
    int32_t dot = vec3_dot(v, v);
    if (dot <= 0) return v;
    uint32_t isqr = q16_inv_sqrt((uint32_t)dot);
    // Multiply by inv_sqrt. Since inv_sqrt is already in log-ready format?
    // No, q16_inv_sqrt returns Q16.16.
    return vec3_init(q16_mul_s(v.x, isqr), q16_mul_s(v.y, isqr), q16_mul_s(v.z, isqr));
}

static inline int32_t vec3_length(Vec3 v) {
    int32_t d2 = vec3_dot(v, v);
    if (d2 <= 0) return 0;
    return (int32_t)q16_sqrt((uint32_t)d2);
}

static inline int32_t vec3_dist(Vec3 a, Vec3 b) {
    return vec3_length(vec3_sub(a, b));
}

static inline Vec3 mat3_mul_vec(const Mat3 *M, Vec3 v) {
    int32_t x = v.x, y = v.y, z = v.z;
    Vec3 r;
    r.x = (int32_t)(((int64_t)M->m[0][0] * x + (int64_t)M->m[0][1] * y + (int64_t)M->m[0][2] * z) >> Q16_S);
    r.y = (int32_t)(((int64_t)M->m[1][0] * x + (int64_t)M->m[1][1] * y + (int64_t)M->m[1][2] * z) >> Q16_S);
    r.z = (int32_t)(((int64_t)M->m[2][0] * x + (int64_t)M->m[2][1] * y + (int64_t)M->m[2][2] * z) >> Q16_S);
    return r;
}

static inline Mat3 mat3_mul_mat(const Mat3 *A, const Mat3 *B) {
    Mat3 R;
    for (int i = 0; i < 3; ++i) {
        int32_t a0 = A->m[i][0], a1 = A->m[i][1], a2 = A->m[i][2];
        for (int j = 0; j < 3; ++j) {
            R.m[i][j] = (int32_t)(((int64_t)a0 * B->m[0][j] +
                                   (int64_t)a1 * B->m[1][j] +
                                   (int64_t)a2 * B->m[2][j]) >> Q16_S);
        }
    }
    return R;
}

static inline Mat3 mat3_rotation_euler(uint16_t ax, uint16_t ay, uint16_t az) {
    int32_t sx = sin_q16(ax), cx = cos_q16(ax);
    int32_t sy = sin_q16(ay), cy = cos_q16(ay);
    int32_t sz = sin_q16(az), cz = cos_q16(az);

    Mat3 M;
    // Order: ZYX. Using intermediate 64-bit precision where safe (max 2^48 for sin/cos products)
    M.m[0][0] = (int32_t)(((int64_t)cz * cy) >> Q16_S);
    M.m[0][1] = (int32_t)(((int64_t)cz * sy * sx) >> 32) - (int32_t)(((int64_t)sz * cx) >> Q16_S);
    M.m[0][2] = (int32_t)(((int64_t)cz * sy * cx) >> 32) + (int32_t)(((int64_t)sz * sx) >> Q16_S);

    M.m[1][0] = (int32_t)(((int64_t)sz * cy) >> Q16_S);
    M.m[1][1] = (int32_t)(((int64_t)sz * sy * sx) >> 32) + (int32_t)(((int64_t)cz * cx) >> Q16_S);
    M.m[1][2] = (int32_t)(((int64_t)sz * sy * cx) >> 32) - (int32_t)(((int64_t)cz * sx) >> Q16_S);

    M.m[2][0] = -sy;
    M.m[2][1] = (int32_t)(((int64_t)cy * sx) >> Q16_S);
    M.m[2][2] = (int32_t)(((int64_t)cy * cx) >> Q16_S);
    return M;
}

static inline Vec3 project_perspective(Vec3 v, int32_t focal) {
    int32_t denom = v.z + focal;
    if (denom == 0) denom = 1;
    return vec3_init(q16_div_s(q16_mul_s(v.x, focal), denom),
                     q16_div_s(q16_mul_s(v.y, focal), denom),
                     v.z);
}

static inline Vec3 project_perspective_ap(Vec3 v, int32_t focal) {
    int32_t denom = v.z + focal;
    if (denom <= 0) denom = 1;
    int32_t log_focal = log2_q8((uint32_t)focal);
    int32_t log_denom = log2_q8((uint32_t)denom);
    int32_t log_factor = log_focal - log_denom;

    Vec3 r;
    r.x = (v.x == 0) ? 0 : (int32_t)exp2_q8(log2_q8((uint32_t)(v.x > 0 ? v.x : -v.x)) + log_factor);
    if (v.x < 0) r.x = -r.x;
    r.y = (v.y == 0) ? 0 : (int32_t)exp2_q8(log2_q8((uint32_t)(v.y > 0 ? v.y : -v.y)) + log_factor);
    if (v.y < 0) r.y = -r.y;
    r.z = v.z;
    return r;
}

static inline Mat4 mat4_identity() {
    Mat4 r;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r.m[i][j] = (i == j) ? Q16_ONE : 0;
        }
    }
    return r;
}

static inline Mat4 mat4_mul(const Mat4 *A, const Mat4 *B) {
    Mat4 R;
    for (int i = 0; i < 4; i++) {
        int32_t a0 = A->m[i][0], a1 = A->m[i][1], a2 = A->m[i][2], a3 = A->m[i][3];
        for (int j = 0; j < 4; j++) {
            R.m[i][j] = (int32_t)(((int64_t)a0 * B->m[0][j] +
                                   (int64_t)a1 * B->m[1][j] +
                                   (int64_t)a2 * B->m[2][j] +
                                   (int64_t)a3 * B->m[3][j]) >> Q16_S);
        }
    }
    return R;
}

static inline Vec4 mat4_mul_vec4(const Mat4 *M, Vec4 v) {
    int32_t x = v.x, y = v.y, z = v.z, w = v.w;
    Vec4 r;
    r.x = (int32_t)(((int64_t)M->m[0][0] * x + (int64_t)M->m[0][1] * y + (int64_t)M->m[0][2] * z + (int64_t)M->m[0][3] * w) >> Q16_S);
    r.y = (int32_t)(((int64_t)M->m[1][0] * x + (int64_t)M->m[1][1] * y + (int64_t)M->m[1][2] * z + (int64_t)M->m[1][3] * w) >> Q16_S);
    r.z = (int32_t)(((int64_t)M->m[2][0] * x + (int64_t)M->m[2][1] * y + (int64_t)M->m[2][2] * z + (int64_t)M->m[2][3] * w) >> Q16_S);
    r.w = (int32_t)(((int64_t)M->m[3][0] * x + (int64_t)M->m[3][1] * y + (int64_t)M->m[3][2] * z + (int64_t)M->m[3][3] * w) >> Q16_S);
    return r;
}

static inline Mat4 mat4_translation(int32_t x, int32_t y, int32_t z) {
    Mat4 r = mat4_identity();
    r.m[0][3] = x;
    r.m[1][3] = y;
    r.m[2][3] = z;
    return r;
}

static inline Mat4 mat4_scaling(int32_t x, int32_t y, int32_t z) {
    Mat4 r = mat4_identity();
    r.m[0][0] = x;
    r.m[1][1] = y;
    r.m[2][2] = z;
    return r;
}

static inline Mat4 mat4_perspective(int32_t focal) {
    Mat4 r;
    for(int i=0; i<4; i++) for(int j=0; j<4; j++) r.m[i][j] = 0;
    r.m[0][0] = focal;
    r.m[1][1] = focal;
    r.m[2][2] = Q16_ONE;
    r.m[2][3] = 0;
    r.m[3][2] = Q16_ONE;
    r.m[3][3] = focal;
    return r;
}

static inline Vec3 mat4_mul_vec3(const Mat4 *M, Vec3 v) {
    Vec3 r;
    r.x = (int32_t)(((int64_t)M->m[0][0] * v.x + (int64_t)M->m[0][1] * v.y + (int64_t)M->m[0][2] * v.z) >> Q16_S) + M->m[0][3];
    r.y = (int32_t)(((int64_t)M->m[1][0] * v.x + (int64_t)M->m[1][1] * v.y + (int64_t)M->m[1][2] * v.z) >> Q16_S) + M->m[1][3];
    r.z = (int32_t)(((int64_t)M->m[2][0] * v.x + (int64_t)M->m[2][1] * v.y + (int64_t)M->m[2][2] * v.z) >> Q16_S) + M->m[2][3];
    return r;
}

static inline Mat4 mat4_rotation_x(uint16_t angle) {
    int32_t s = sin_q16(angle), c = cos_q16(angle);
    Mat4 r = mat4_identity();
    r.m[1][1] = c; r.m[1][2] = -s;
    r.m[2][1] = s; r.m[2][2] = c;
    return r;
}

static inline Mat4 mat4_rotation_y(uint16_t angle) {
    int32_t s = sin_q16(angle), c = cos_q16(angle);
    Mat4 r = mat4_identity();
    r.m[0][0] = c;  r.m[0][2] = s;
    r.m[2][0] = -s; r.m[2][2] = c;
    return r;
}

static inline Mat4 mat4_rotation_z(uint16_t angle) {
    int32_t s = sin_q16(angle), c = cos_q16(angle);
    Mat4 r = mat4_identity();
    r.m[0][0] = c; r.m[0][1] = -s;
    r.m[1][0] = s; r.m[1][1] = c;
    return r;
}

static inline Quat quat_from_axis_angle(int32_t ax, int32_t ay, int32_t az, uint16_t angle) {
    int32_t s = sin_q16(angle >> 1);
    int32_t c = cos_q16(angle >> 1);
    Quat q = {c,
              (int32_t)(((int64_t)ax * s) >> Q16_S),
              (int32_t)(((int64_t)ay * s) >> Q16_S),
              (int32_t)(((int64_t)az * s) >> Q16_S)};
    return q;
}

static inline Quat quat_mul_quat(Quat a, Quat b) {
    int32_t aw = a.w, ax = a.x, ay = a.y, az = a.z;
    int32_t bw = b.w, bx = b.x, by = b.y, bz = b.z;
    Quat r;
    r.w = (int32_t)(((int64_t)aw * bw - (int64_t)ax * bx - (int64_t)ay * by - (int64_t)az * bz) >> Q16_S);
    r.x = (int32_t)(((int64_t)aw * bx + (int64_t)ax * bw + (int64_t)ay * bz - (int64_t)az * by) >> Q16_S);
    r.y = (int32_t)(((int64_t)aw * by - (int64_t)ax * bz + (int64_t)ay * bw + (int64_t)az * bx) >> Q16_S);
    r.z = (int32_t)(((int64_t)aw * bz + (int64_t)ax * by - (int64_t)ay * bx + (int64_t)az * bw) >> Q16_S);
    return r;
}

static inline Quat quat_normalize(Quat q) {
    int32_t d2 = (int32_t)(((int64_t)q.w * q.w + (int64_t)q.x * q.x + (int64_t)q.y * q.y + (int64_t)q.z * q.z) >> Q16_S);
    if (d2 <= 0) return q;
    uint32_t isqr = q16_inv_sqrt((uint32_t)d2);
    Quat r;
    r.w = q16_mul_s(q.w, isqr);
    r.x = q16_mul_s(q.x, isqr);
    r.y = q16_mul_s(q.y, isqr);
    r.z = q16_mul_s(q.z, isqr);
    return r;
}

static inline Quat quat_nlerp(Quat a, Quat b, int32_t t) {
    Quat r;
    r.w = q16_lerp(a.w, b.w, t);
    r.x = q16_lerp(a.x, b.x, t);
    r.y = q16_lerp(a.y, b.y, t);
    r.z = q16_lerp(a.z, b.z, t);
    return quat_normalize(r);
}

static inline Vec3 quat_rotate_vec(Quat q, Vec3 v) {
    // v' = v + 2*q_vec x (q_vec x v + q.w * v)
    int32_t tx = (int32_t)(((int64_t)q.y * v.z - (int64_t)q.z * v.y) >> (Q16_S - 1));
    int32_t ty = (int32_t)(((int64_t)q.z * v.x - (int64_t)q.x * v.z) >> (Q16_S - 1));
    int32_t tz = (int32_t)(((int64_t)q.x * v.y - (int64_t)q.y * v.x) >> (Q16_S - 1));

    int32_t res_x = v.x + (int32_t)(((int64_t)q.w * tx + (int64_t)q.y * tz - (int64_t)q.z * ty) >> Q16_S);
    int32_t res_y = v.y + (int32_t)(((int64_t)q.w * ty + (int64_t)q.z * tx - (int64_t)q.x * tz) >> Q16_S);
    int32_t res_z = v.z + (int32_t)(((int64_t)q.w * tz + (int64_t)q.x * ty - (int64_t)q.y * tx) >> Q16_S);
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

    return project_perspective_ap(world, focal);
}

} // namespace FMT

#endif
