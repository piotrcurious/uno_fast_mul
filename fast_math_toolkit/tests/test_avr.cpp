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

__attribute__((noinline)) FMT::Vec3 test_pipeline(FMT::Vec3 v, int32_t scale, uint16_t ax, uint16_t ay, uint16_t az, FMT::Vec3 trans, int32_t focal) {
    return FMT::pipeline_mvp(v, scale, ax, ay, az, trans, focal);
}

__attribute__((noinline)) FMT::Vec3 test_pipeline_fused(FMT::Vec3 v, int32_t scale, uint16_t ax, uint16_t ay, uint16_t az, FMT::Vec3 trans, int32_t focal) {
    return FMT::pipeline_mvp_fused(v, scale, ax, ay, az, trans, focal);
}

int main(void) {
    UBRR0H = 0;
    UBRR0L = 103;
    UCSR0B = (1 << TXEN0);

    FILE uartout;
    fdev_setup_stream(&uartout, uart_putchar, NULL, _FDEV_SETUP_WRITE);
    stdout = &uartout;

    init_timer();

    printf("AVR FMT Ring/Fused Benchmarks\n");

    FMT::Vec3 v = {0, 0x10000, 0};
    int32_t scale = 0x10000;
    FMT::Vec3 trans = {0, 0, 0x200000};
    int32_t focal = 0x1000000;

    start_timer();
    FMT::Vec3 r1 = test_pipeline(v, scale, 0, 0, 0, trans, focal);
    uint16_t c1 = stop_timer();
    printf("pipeline_mvp: %u cycles\n", c1 - 4);

    start_timer();
    FMT::Vec3 r2 = test_pipeline_fused(v, scale, 0, 0, 0, trans, focal);
    uint16_t c2 = stop_timer();
    printf("pipeline_mvp_fused: %u cycles\n", c2 - 4);

    printf("DONE %ld %ld\n", r1.y, r2.y);
    while(1);
    return 0;
}
