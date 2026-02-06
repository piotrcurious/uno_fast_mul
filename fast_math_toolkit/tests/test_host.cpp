#include <iostream>
#include <cmath>
#include <iomanip>
#include <stdint.h>
#include <assert.h>

#define INCLUDE_TABLES "arduino_tables_generated.h"
#include "../FMT.h"

using namespace FMT;

void test_ring() {
    std::cout << "Testing FMT_Ring (Log32)..." << std::endl;
    Log32 a = to_log32(100);
    Log32 b = to_log32(200);
    Log32 c = log32_mul(a, b);
    int32_t res = from_log32(c);
    std::cout << "  100 * 200 = " << res << " (exact: 20000)" << std::endl;

    Log32 d = log32_div(c, to_log32(50));
    int32_t res2 = from_log32(d);
    std::cout << "  20000 / 50 = " << res2 << " (exact: 400)" << std::endl;

    Log32 e = log32_pow(to_log32(2), 10.0f);
    int32_t res3 = from_log32(e);
    std::cout << "  2^10 = " << res3 << " (exact: 1024)" << std::endl;
}

void test_fused_pipeline() {
    std::cout << "Testing Fused Pipeline..." << std::endl;
    Vec3 v = {0, 0x10000, 0}; // (0,1,0)
    Vec3 trans = {0, 0, 0x200000}; // 32 in Z
    int32_t focal = 0x1000000; // 256

    Vec3 vp1 = pipeline_mvp(v, 0x10000, 0, 0, 0, trans, focal);
    Vec3 vp2 = pipeline_mvp_fused(v, 0x10000, 0, 0, 0, trans, focal);

    std::cout << "  Standard result: (" << q16_to_float(vp1.x) << ", " << q16_to_float(vp1.y) << ", " << q16_to_float(vp1.z) << ")" << std::endl;
    std::cout << "  Fused result:    (" << q16_to_float(vp2.x) << ", " << q16_to_float(vp2.y) << ", " << q16_to_float(vp2.z) << ")" << std::endl;
}

int main() {
    test_ring();
    test_fused_pipeline();
    return 0;
}
