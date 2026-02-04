#include <Arduino.h>
#include "fast_float.h"

// Note: Ensure fast_float_tables.c is included in your project!

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("Fast Float multiplication/division Demo (BTM)"));

  float a = 12.34f;
  float b = 56.78f;

  float res_mul = fast_mul_f32(a, b);
  float exact_mul = a * b;

  float res_div = fast_div_f32(a, b);
  float exact_div = a / b;

  Serial.print(F("a = ")); Serial.println(a, 4);
  Serial.print(F("b = ")); Serial.println(b, 4);

  Serial.print(F("MUL Approx: ")); Serial.println(res_mul, 4);
  Serial.print(F("MUL Exact:  ")); Serial.println(exact_mul, 4);

  Serial.print(F("DIV Approx: ")); Serial.println(res_div, 4);
  Serial.print(F("DIV Exact:  ")); Serial.println(exact_div, 4);
}

void loop() {
  // Random tests
  float a = (float)random(100, 10000) / 100.0f;
  float b = (float)random(100, 10000) / 100.0f;

  float approx = fast_mul_f32(a, b);
  float exact = a * b;
  float err = abs(approx - exact) / exact;

  Serial.print(a); Serial.print(F(" * ")); Serial.print(b);
  Serial.print(F(" = ")); Serial.print(approx);
  Serial.print(F(" (err ")); Serial.print(err * 100.0, 4); Serial.println(F("%)"));

  delay(2000);
}
