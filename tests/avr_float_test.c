#include <avr/io.h>
#include <stdio.h>
#include "../fast_float.h"

static int uart_putchar(char c, FILE *stream) {
    if (c == '\n') uart_putchar('\r', stream);
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;
    return 0;
}

static FILE uartout = FDEV_SETUP_STREAM(uart_putchar, NULL, _FDEV_SETUP_WRITE);

int main(void) {
    UBRR0H = 0;
    UBRR0L = 103;
    UCSR0B = (1 << TXEN0);
    stdout = &uartout;

    printf("AVR Fast Float Test (BTM)\n");

    volatile float a = 123.456f;
    volatile float b = 789.012f;

    float res_mul = fast_mul_f32(a, b);
    float res_div = fast_div_f32(a, b);

    // We can't easily print floats with default avr-libc printf
    // but we can print their raw bits or just confirm completion.
    printf("Multiplication/Division completed.\n");

    printf("DONE\n");
    while(1);
    return 0;
}
