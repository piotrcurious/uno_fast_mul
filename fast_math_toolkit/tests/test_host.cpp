#include <iostream>
#include <cmath>
#include <iomanip>
#include <stdint.h>
#include <vector>
#include <string>

#define INCLUDE_TABLES "arduino_tables_generated.h"
#include "../FMT.h"

using namespace FMT;

#define EXPECT_NEAR(val, target, tolerance) \
    do { \
        double v = (double)(val); \
        double t = (double)(target); \
        if (std::abs(v - t) > (tolerance)) { \
            std::cout << "FAIL: " << #val << " (" << v << ") expected near " << t << " (tol: " << tolerance << ")" << std::endl; \
        } \
    } while(0)

void test_core() {
    std::cout << "Testing FMT_Core..." << std::endl;
    // fast_msb32
    if (fast_msb32(1) != 0) std::cout << "FAIL: fast_msb32(1)" << std::endl;
    if (fast_msb32(128) != 7) std::cout << "FAIL: fast_msb32(128)" << std::endl;
    if (fast_msb32(65536) != 16) std::cout << "FAIL: fast_msb32(65536)" << std::endl;

    // log2/exp2
    int32_t l = log2_q8(256);
    EXPECT_NEAR(l, 8 << 8, 2);
    EXPECT_NEAR(exp2_q8(l), 256, 2);

    // approx mul/div
    EXPECT_NEAR(mul_u16_ap(100, 50), 5000, 50);
    EXPECT_NEAR(div_u32_u16_ap(10000, 100), 100, 2);
    EXPECT_NEAR(mul_u32_ap(100000, 2), 200000, 2000);
}

void test_fixed() {
    std::cout << "Testing FMT_Fixed..." << std::endl;
    int32_t a = q16_from_float(1.5f);
    int32_t b = q16_from_float(2.0f);
    EXPECT_NEAR(q16_to_float(q16_mul_s(a, b)), 3.0f, 0.001f);
    EXPECT_NEAR(q16_to_float(q16_div_s(b, a)), 1.3333f, 0.001f);
    EXPECT_NEAR(q16_to_float(q16_inv_sqrt(q16_from_float(4.0f))), 0.5f, 0.01f);
    EXPECT_NEAR(q16_to_float(q16_sqrt(q16_from_float(4.0f))), 2.0f, 0.01f);
}

void test_trig() {
    std::cout << "Testing FMT_Trig..." << std::endl;
    EXPECT_NEAR(sin_u16(0), 0, 10);
    EXPECT_NEAR(sin_u16(16384), 32767, 10); // 90 deg
    EXPECT_NEAR(cos_u16(16384), 0, 10);

    // atan2
    EXPECT_NEAR(atan2_u16(0, 100), 0, 10);
    EXPECT_NEAR(atan2_u16(100, 0), 16384, 10); // 90 deg
    EXPECT_NEAR(atan2_u16(100, 100), 8192, 10); // 45 deg
    EXPECT_NEAR(atan2_u16(0, -100), 32768, 10); // 180 deg
    EXPECT_NEAR(atan2_u16(-100, 0), 49152, 10); // 270 deg
}

void test_3d() {
    std::cout << "Testing FMT_3d..." << std::endl;
    Vec3 v1 = vec3_init(q16_from_float(1.0f), 0, 0);
    Vec3 v2 = vec3_init(0, q16_from_float(1.0f), 0);

    EXPECT_NEAR(q16_to_float(vec3_dot(v1, v2)), 0.0f, 0.001f);

    Vec3 v3 = vec3_cross(v1, v2);
    EXPECT_NEAR(q16_to_float(v3.z), 1.0f, 0.001f);

    Vec3 vn = vec3_normalize(vec3_init(0x20000, 0, 0));
    EXPECT_NEAR(q16_to_float(vn.x), 1.0f, 0.01f);

    Mat3 Ry = mat3_rotation_euler(0, 16384, 0);
    Vec3 vry = mat3_mul_vec(&Ry, v1);
    EXPECT_NEAR(q16_to_float(vry.z), -1.0f, 0.01f);

    Quat q = quat_from_axis_angle(0, 0x10000, 0, 16384);
    Vec3 vrq = quat_rotate_vec(q, v1);
    EXPECT_NEAR(q16_to_float(vrq.z), -1.0f, 0.01f);

    Mat3 A = mat3_rotation_euler(0, 16384, 0); // 90 deg around Y
    Mat3 B = mat3_rotation_euler(0, 0, 16384); // 90 deg around Z
    Mat3 C = mat3_mul_mat(&A, &B);
    Vec3 vr = mat3_mul_vec(&C, v1);
    // Rotate (1,0,0) by Z then Y -> (0,1,0) then (0,1,0) wait.
    // Z rotate (1,0,0) -> (0,1,0). Y rotate (0,1,0) -> (0,1,0).
    EXPECT_NEAR(q16_to_float(vr.y), 1.0f, 0.01f);

    Quat q1 = quat_from_axis_angle(0, 0x10000, 0, 16384);
    Quat q2 = quat_from_axis_angle(0, 0x10000, 0, 16384);
    Quat q3 = quat_mul_quat(q1, q2); // 180 deg around Y
    Vec3 vr2 = quat_rotate_vec(q3, v1);
    EXPECT_NEAR(q16_to_float(vr2.x), -1.0f, 0.01f);

    Mat4 M1 = mat4_translation(q16_from_float(10.0f), 0, 0);
    Mat4 M2 = mat4_translation(0, q16_from_float(5.0f), 0);
    Mat4 M3 = mat4_mul(&M1, &M2);
    EXPECT_NEAR(q16_to_float(M3.m[0][3]), 10.0f, 0.01f);
    EXPECT_NEAR(q16_to_float(M3.m[1][3]), 5.0f, 0.01f);

    Vec3 v4 = {0x10000, 0, 0};
    Vec3 vt = mat4_mul_vec3(&M1, v4);
    EXPECT_NEAR(q16_to_float(vt.x), 11.0f, 0.01f);

    Mat4 Ms = mat4_scaling(q16_from_float(2.0f), q16_from_float(0.5f), q16_from_float(1.0f));
    Vec3 vs = mat4_mul_vec3(&Ms, v4);
    EXPECT_NEAR(q16_to_float(vs.x), 2.0f, 0.01f);
    EXPECT_NEAR(q16_to_float(vs.y), 0.0f, 0.01f);

    // Ray-Sphere
    Vec3 ray_O = {0, 0, 0};
    Vec3 ray_D = {0, 0, 0x10000};
    Vec3 sphere_C = {0, 0, 0x50000};
    int32_t sphere_r = 0x10000;
    int32_t ray_t;
    if (ray_sphere_intersect(ray_O, ray_D, sphere_C, sphere_r, &ray_t)) {
        EXPECT_NEAR(q16_to_float(ray_t), 4.0f, 0.1f);
    } else {
        std::cout << "FAIL: ray_sphere_intersect" << std::endl;
    }

    Mat4 M_rot = mat4_rotation_y(16384); // 90 deg
    M_rot.m[0][3] = q16_from_float(5.0f);
    Mat4 M_inv = mat4_inverse_affine_rot(&M_rot);
    Mat4 M_prod = mat4_mul(&M_rot, &M_inv);
    EXPECT_NEAR(q16_to_float(M_prod.m[0][0]), 1.0f, 0.01f);
    EXPECT_NEAR(q16_to_float(M_prod.m[1][1]), 1.0f, 0.01f);
    EXPECT_NEAR(q16_to_float(M_prod.m[2][2]), 1.0f, 0.01f);
    EXPECT_NEAR(q16_to_float(M_prod.m[0][3]), 0.0f, 0.01f);

    Mat4 Mp = mat4_perspective(0x10000); // focal 1.0
    Vec4 v5 = {0, 0x10000, 0x10000, 0x10000}; // (0,1,1)
    Vec4 vp5 = mat4_mul_vec4(&Mp, v5);
    // x = 0, y = 1*1 = 1, z = 1, w = 1*1 + 1*1 = 2
    // perspective divide y/w = 0.5
    EXPECT_NEAR(q16_to_float(vp5.y) / q16_to_float(vp5.w), 0.5f, 0.01f);

    // Ray-Plane
    Vec3 plane_n = {0, 0x10000, 0}; // Y axis
    int32_t plane_d = -0x50000; // y = 5
    Vec3 rO = {0, 0, 0};
    Vec3 rD = {0, 0x10000, 0};
    int32_t rt;
    if (ray_plane_intersect(rO, rD, plane_n, plane_d, &rt)) {
        EXPECT_NEAR(q16_to_float(rt), 5.0f, 0.1f);
    } else {
        std::cout << "FAIL: ray_plane_intersect" << std::endl;
    }
}

void test_ring() {
    std::cout << "Testing FMT_Ring..." << std::endl;
    Log32 la = to_log32(100);
    Log32 lb = to_log32(5);
    EXPECT_NEAR(from_log32(log32_mul(la, lb)), 500, 5);
    EXPECT_NEAR(from_log32(log32_div(la, lb)), 20, 1);

    Log32 lsum = log32_add(to_log32(100), to_log32(200));
    EXPECT_NEAR(from_log32(lsum), 300, 5);
}

void test_fused_pipeline() {
    std::cout << "Testing Fused Pipeline..." << std::endl;
    Vec3 v = {0, 0x10000, 0}; // (0,1,0)
    Vec3 trans = {0, 0, 0x200000}; // 32 in Z
    int32_t focal = 0x1000000; // 256

    Vec3 vp1 = pipeline_mvp(v, 0x10000, 0, 0, 0, trans, focal);
    Vec3 vp2 = pipeline_mvp_fused(v, 0x10000, 0, 0, 0, trans, focal);

    EXPECT_NEAR(q16_to_float(vp1.y), q16_to_float(vp2.y), 0.1f);
}

void test_utils() {
    std::cout << "Testing FMT_Utils..." << std::endl;
    // Perspective table should return 256 for z=0 if focal=256
    EXPECT_NEAR(get_perspective(0), 256, 1);
}

int main() {
    test_core();
    test_fixed();
    test_trig();
    test_3d();
    test_ring();
    test_fused_pipeline();
    test_utils();
    std::cout << "Host tests completed." << std::endl;
    return 0;
}
