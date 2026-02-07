#include <avr/io.h>
#include <stdio.h>
#include <stdint.h>

#define INCLUDE_TABLES "arduino_tables_generated.h"
#include "../FMT.h"

static int uart_putchar(char c, FILE *stream) {
    if (c == '\n') uart_putchar('\r', stream);
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;
    return 0;
}

void init_timer() {
    TCCR1A = 0;
    TCCR1B = 0;
}

inline void start_timer() {
    TCNT1 = 0;
    TCCR1B = (1 << CS10);
}

inline uint16_t stop_timer() {
    uint16_t t = TCNT1;
    TCCR1B = 0;
    return t;
}

volatile int32_t g_sink;

// Wrapper functions to ensure realistic cycle counts
__attribute__((noinline)) int32_t bench_log2(uint32_t v) { return FMT::log2_q8(v); }
__attribute__((noinline)) uint32_t bench_exp2(int32_t v) { return FMT::exp2_q8(v); }
__attribute__((noinline)) int32_t bench_q16_mul_s(int32_t a, int32_t b) { return FMT::q16_mul_s(a, b); }
__attribute__((noinline)) int32_t bench_q16_div_s(int32_t a, int32_t b) { return FMT::q16_div_s(a, b); }
__attribute__((noinline)) int32_t bench_q16_div_s_ap(int32_t a, int32_t b) { return FMT::q16_div_s_ap(a, b); }
__attribute__((noinline)) FMT::Vec3 bench_mat3_mul_vec(const FMT::Mat3* M, FMT::Vec3 v) { return FMT::mat3_mul_vec(M, v); }
__attribute__((noinline)) FMT::Mat3 bench_mat3_mul_mat(const FMT::Mat3* A, const FMT::Mat3* B) { return FMT::mat3_mul_mat(A, B); }
__attribute__((noinline)) FMT::Quat bench_quat_mul_quat(FMT::Quat a, FMT::Quat b) { return FMT::quat_mul_quat(a, b); }
__attribute__((noinline)) FMT::Vec3 bench_quat_rotate_vec(FMT::Quat q, FMT::Vec3 v) { return FMT::quat_rotate_vec(q, v); }
__attribute__((noinline)) FMT::Quat bench_quat_normalize(FMT::Quat q) { return FMT::quat_normalize(q); }
__attribute__((noinline)) int32_t bench_vec3_length(FMT::Vec3 v) { return FMT::vec3_length(v); }
__attribute__((noinline)) FMT::Log32 bench_log32_add(FMT::Log32 a, FMT::Log32 b) { return FMT::log32_add(a, b); }
__attribute__((noinline)) int16_t bench_sin(uint16_t a) { return FMT::sin_u16(a); }
__attribute__((noinline)) uint16_t bench_atan2(int32_t y, int32_t x) { return FMT::atan2_u16(y, x); }
__attribute__((noinline)) FMT::Mat3 bench_rotation(uint16_t x, uint16_t y, uint16_t z) { return FMT::mat3_rotation_euler(x, y, z); }
__attribute__((noinline)) FMT::Mat4 bench_mat4_mul(const FMT::Mat4* A, const FMT::Mat4* B) { return FMT::mat4_mul(A, B); }
__attribute__((noinline)) FMT::Mat4 bench_mat4_mul_affine(const FMT::Mat4* A, const FMT::Mat4* B) { return FMT::mat4_mul_affine(A, B); }

int main(void) {
    UBRR0H = 0;
    UBRR0L = 103;
    UCSR0B = (1 << TXEN0);

    FILE uartout;
    fdev_setup_stream(&uartout, uart_putchar, NULL, _FDEV_SETUP_WRITE);
    stdout = &uartout;

    init_timer();

    printf("AVR FMT Final Benchmarks\n");

    uint32_t u1 = 1234567;
    int32_t s1 = 123456, s2 = -123;

    asm volatile("" : "+r"(u1), "+r"(s1), "+r"(s2));

    start_timer();
    g_sink = bench_log2(u1);
    uint16_t c1 = stop_timer();
    printf("log2_q8: %u cycles\n", c1 - 4);

    start_timer();
    g_sink = bench_exp2(s1);
    uint16_t c2 = stop_timer();
    printf("exp2_q8: %u cycles\n", c2 - 4);

    start_timer();
    g_sink = bench_q16_mul_s(s1, s2);
    uint16_t c3 = stop_timer();
    printf("q16_mul_s: %u cycles\n", c3 - 4);

    start_timer();
    g_sink = bench_q16_div_s(s1, s2);
    uint16_t c4 = stop_timer();
    printf("q16_div_s (exact): %u cycles\n", c4 - 4);

    start_timer();
    g_sink = bench_q16_div_s_ap(s1, s2);
    uint16_t c4ap = stop_timer();
    printf("q16_div_s (approx): %u cycles\n", c4ap - 4);

    FMT::Vec3 v1 = {0x10000, 0, 0};
    FMT::Mat3 M = {{ {0x10000, 0, 0}, {0, 0x10000, 0}, {0, 0, 0x10000} }};
    FMT::Quat Q = {0x10000, 0, 0, 0};
    asm volatile("" : "+g"(v1), "+g"(M), "+g"(Q));

    start_timer();
    FMT::Vec3 rv1 = bench_mat3_mul_vec(&M, v1);
    uint16_t c6 = stop_timer();
    g_sink = rv1.x;
    printf("mat3_mul_vec: %u cycles\n", c6 - 4);

    start_timer();
    FMT::Mat3 RM = bench_mat3_mul_mat(&M, &M);
    uint16_t c6m = stop_timer();
    g_sink = RM.m[0][0];
    printf("mat3_mul_mat: %u cycles\n", c6m - 4);

    start_timer();
    FMT::Quat RQ = bench_quat_mul_quat(Q, Q);
    uint16_t c7q = stop_timer();
    g_sink = RQ.w;
    printf("quat_mul_quat: %u cycles\n", c7q - 4);

    start_timer();
    FMT::Vec3 rv2 = bench_quat_rotate_vec(Q, v1);
    uint16_t c7 = stop_timer();
    g_sink = rv2.x;
    printf("quat_rotate_vec: %u cycles\n", c7 - 4);

    start_timer();
    FMT::Quat nq = bench_quat_normalize(Q);
    uint16_t c7n = stop_timer();
    g_sink = nq.w;
    printf("quat_normalize: %u cycles\n", c7n - 4);

    start_timer();
    int32_t vlen = bench_vec3_length(v1);
    uint16_t c7l = stop_timer();
    g_sink = vlen;
    printf("vec3_length: %u cycles\n", c7l - 4);

    start_timer();
    FMT::Log32 la = FMT::to_log32(100);
    FMT::Log32 lb = FMT::to_log32(200);
    FMT::Log32 lsum = bench_log32_add(la, lb);
    uint16_t c7ls = stop_timer();
    g_sink = lsum.lval;
    printf("log32_add: %u cycles\n", c7ls - 4);

    start_timer();
    int16_t s16 = bench_sin(u1);
    uint16_t c8 = stop_timer();
    g_sink = s16;
    printf("sin_u16: %u cycles\n", c8 - 4);

    start_timer();
    uint16_t a16 = bench_atan2(s1, s2);
    uint16_t c8a = stop_timer();
    g_sink = a16;
    printf("atan2_u16: %u cycles\n", c8a - 4);

    start_timer();
    FMT::Mat4 M4 = FMT::mat4_identity();
    FMT::Mat4 RM4 = bench_mat4_mul(&M4, &M4);
    uint16_t c10 = stop_timer();
    g_sink = RM4.m[0][0];
    printf("mat4_mul: %u cycles\n", c10 - 4);

    start_timer();
    FMT::Mat4 RM4a = bench_mat4_mul_affine(&M4, &M4);
    uint16_t c10a = stop_timer();
    g_sink = RM4a.m[0][0];
    printf("mat4_mul_affine: %u cycles\n", c10a - 4);

    start_timer();
    FMT::Mat3 RM2 = bench_rotation(0, 16384, 0);
    uint16_t c9 = stop_timer();
    g_sink = RM2.m[0][0];
    printf("mat3_rotation_euler: %u cycles\n", c9 - 4);

    printf("DONE\n");
    while(1);
    return 0;
}
