#include <avr/io.h>
#include <stdio.h>
#include "fast_mul.h"

// Simple UART for simavr
static int uart_putchar(char c, FILE *stream) {
    if (c == '\n') uart_putchar('\r', stream);
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;
    return 0;
}

static FILE uartout = FDEV_SETUP_STREAM(uart_putchar, NULL, _FDEV_SETUP_WRITE);

int main(void) {
    // UART init (simavr doesn't care much about baud rate, but good to set)
    UBRR0H = 0;
    UBRR0L = 103; // 9600 baud at 16MHz
    UCSR0B = (1 << TXEN0);

    stdout = &uartout;

    printf("AVR Fast Mul Test Start\n");

    uint16_t pairs[][2] = {
        {1, 1},
        {123, 456},
        {30000, 2},
        {65535, 65535},
        {1023, 511},
        {500, 500},
        {0, 100},
        {100, 0}
    };

    for (int i = 0; i < 8; i++) {
        uint16_t a = pairs[i][0];
        uint16_t b = pairs[i][1];
        uint32_t exact = (uint32_t)a * (uint32_t)b;
        uint32_t approx = fast_log_mul_u16(a, b);
        printf("%u * %u: exact %lu, approx %lu\n", a, b, exact, approx);
    }

    // Some "random" tests using LCG
    uint16_t seed = 0x1234;
    for (int i = 0; i < 20; i++) {
        seed = seed * 32719 + 3;
        uint16_t a = seed;
        seed = seed * 32719 + 3;
        uint16_t b = seed;
        uint32_t exact = (uint32_t)a * (uint32_t)b;
        uint32_t approx = fast_log_mul_u16(a, b);
        printf("%u * %u: exact %lu, approx %lu\n", a, b, exact, approx);
    }

    printf("DONE\n");

    // To signal simavr to stop, we can use a special port if configured,
    // but usually we just let it run or kill it.
    // In simavr, we can use a special opcode or just a loop.
    while(1);
    return 0;
}
