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
