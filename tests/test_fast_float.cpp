#include <iostream>
#include <iomanip>
#include <cmath>
#include <stdint.h>
#include "../fast_float.h"

int main() {
    std::cout << "Testing fast_float multiplication and division (BTM)..." << std::endl;

    float tests[][2] = {
        {1.0f, 1.0f},
        {123.456f, 789.012f},
        {0.001f, 1000.0f},
        {1e-5f, 1e5f},
        {65535.0f, 65535.0f},
        {-1.0f, 5.0f},
        {2.0f, -3.0f},
        {-10.0f, -10.0f}
    };

    for (int i = 0; i < 8; i++) {
        float a = tests[i][0];
        float b = tests[i][1];
        float res_mul = fast_mul_f32(a, b);
        float exact_mul = a * b;
        float err_mul = std::abs(res_mul - exact_mul) / (std::abs(exact_mul) + 1e-20f);

        float res_div = fast_div_f32(a, b);
        float exact_div = a / b;
        float err_div = std::abs(res_div - exact_div) / (std::abs(exact_div) + 1e-20f);

        std::cout << a << " * " << b << ": approx " << res_mul << ", exact " << exact_mul << ", err " << err_mul * 100.0 << "%" << std::endl;
        std::cout << a << " / " << b << ": approx " << res_div << ", exact " << exact_div << ", err " << err_div * 100.0 << "%" << std::endl;
    }

    int samples = 100000;
    double total_err_mul = 0;
    double total_err_div = 0;
    for (int i = 0; i < samples; i++) {
        float a = (float)rand() / (float)RAND_MAX * 1000.0f + 0.1f;
        float b = (float)rand() / (float)RAND_MAX * 1000.0f + 0.1f;

        float res_mul = fast_mul_f32(a, b);
        float exact_mul = a * b;
        total_err_mul += std::abs(res_mul - exact_mul) / std::abs(exact_mul);

        float res_div = fast_div_f32(a, b);
        float exact_div = a / b;
        total_err_div += std::abs(res_div - exact_div) / std::abs(exact_div);
    }

    std::cout << "\n--- Statistics (100k samples) ---" << std::endl;
    std::cout << "Average relative error (MUL): " << (total_err_mul / samples) * 100.0 << "%" << std::endl;
    std::cout << "Average relative error (DIV): " << (total_err_div / samples) * 100.0 << "%" << std::endl;

    return 0;
}
