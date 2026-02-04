#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <stdint.h>
#include "fast_mul.h"

int main() {
    std::cout << "Starting exhaustive host-based test..." << std::endl;

    uint64_t total_tests = 0;
    double total_rel_error = 0;
    double max_rel_error = 0;
    uint32_t max_err_a = 0, max_err_b = 0;
    uint64_t exact_matches = 0;

    for (uint32_t a = 1; a <= 65535; ++a) {
        if (a % 10000 == 0) {
            std::cout << "Progress: a = " << a << "/65535" << std::endl;
        }
        for (uint32_t b = 1; b <= 65535; ++b) {
            uint32_t approx = fast_log_mul_u16((uint16_t)a, (uint16_t)b);
            uint32_t exact = a * b;

            total_tests++;
            if (approx == exact) {
                exact_matches++;
                continue;
            }

            double err = std::abs((double)approx - (double)exact);
            double rel_err = (err / (double)exact) * 100.0;

            total_rel_error += rel_err;
            if (rel_err > max_rel_error) {
                max_rel_error = rel_err;
                max_err_a = a;
                max_err_b = b;
            }
        }
    }

    std::cout << "\n--- Statistics ---" << std::endl;
    std::cout << "Total tests: " << total_tests << std::endl;
    std::cout << "Exact matches: " << exact_matches << std::endl;
    std::cout << "Average relative error: " << (total_rel_error / total_tests) << "%" << std::endl;
    std::cout << "Max relative error: " << max_rel_error << "% at " << max_err_a << " * " << max_err_b << std::endl;

    return 0;
}
