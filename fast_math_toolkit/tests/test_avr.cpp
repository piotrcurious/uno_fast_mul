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
__attribute__((noinline)) FMT::Vec3 bench_quat_rotate_vec(FMT::Quat q, FMT::Vec3 v) { return FMT::quat_rotate_vec(q, v); }

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
    FMT::Vec3 rv2 = bench_quat_rotate_vec(Q, v1);
    uint16_t c7 = stop_timer();
    g_sink = rv2.x;
    printf("quat_rotate_vec: %u cycles\n", c7 - 4);

    printf("DONE\n");
    while(1);
    return 0;
}
