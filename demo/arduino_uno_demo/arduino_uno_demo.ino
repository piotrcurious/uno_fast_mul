/*
  arduino_uno_demo.ino
  Arduino Uno demo using the Fast Math Toolkit (FMT).
  - Requires: Adafruit_GFX, Adafruit_ILI9341
  - Requires generated arduino_tables.h / .cpp in the include path
  - Uses FMT for all transformations.
*/

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// Include Fast Math Toolkit
#include "../../fast_math_toolkit/FMT.h"

// Pin definitions
#define TFT_CS    10
#define TFT_DC     9
#define TFT_RST    8

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

const char* rows[] = {
  "HELLO WORLD",
  "FMT TOOLKIT DEMO",
  "FAST 3D MATH"
};
const uint8_t ROW_COUNT = sizeof(rows)/sizeof(rows[0]);

const int16_t SCREEN_W = 240;
const int16_t SCREEN_H = 320;
const uint16_t CHAR_W = 6;
const uint16_t CHAR_H = 8;

uint16_t global_frame = 0;

// Small 5x7 font (re-included for standalone sketch)
const uint8_t PROGMEM font5x7[] = {
  0x00,0x00,0x00,0x00,0x00, 0x7C,0x12,0x11,0x12,0x7C, 0x7F,0x49,0x49,0x49,0x36,
  0x3E,0x41,0x41,0x41,0x22, 0x7F,0x41,0x41,0x22,0x1C, 0x7F,0x49,0x49,0x49,0x41,
  0x7F,0x09,0x09,0x09,0x01, 0x3E,0x41,0x49,0x49,0x7A, 0x7F,0x08,0x08,0x08,0x7F,
  0x00,0x41,0x7F,0x41,0x00, 0x20,0x40,0x41,0x3F,0x01, 0x7F,0x08,0x14,0x22,0x41,
  0x7F,0x40,0x40,0x40,0x40, 0x7F,0x02,0x0C,0x02,0x7F, 0x7F,0x04,0x08,0x10,0x7F,
  0x3E,0x41,0x41,0x41,0x3E, 0x7F,0x09,0x09,0x09,0x06, 0x3E,0x41,0x51,0x21,0x5E,
  0x7F,0x09,0x19,0x29,0x46, 0x46,0x49,0x49,0x49,0x31, 0x01,0x01,0x7F,0x01,0x01,
  0x3F,0x40,0x40,0x40,0x3F, 0x1F,0x20,0x40,0x20,0x1F, 0x3F,0x40,0x38,0x40,0x3F,
  0x63,0x14,0x08,0x14,0x63, 0x07,0x08,0x70,0x08,0x07, 0x61,0x51,0x49,0x45,0x43,
  0x00,0x60,0x60,0x00,0x00, 0x00,0x80,0x60,0x00,0x00, 0x00,0x00,0x7D,0x00,0x00,
  0x02,0x01,0x59,0x06,0x02
};

const uint8_t* font_ptr_for_char(char c) {
  if (c >= 'A' && c <= 'Z') return font5x7 + (1 + (c - 'A')) * 5;
  if (c == '.') return font5x7 + 27 * 5;
  if (c == ',') return font5x7 + 28 * 5;
  if (c == '!') return font5x7 + 29 * 5;
  if (c == '?') return font5x7 + 30 * 5;
  return font5x7 + 0; // space
}

void render_frame() {
  tft.fillScreen(ILI9341_BLACK);

  for (uint8_t r = 0; r < ROW_COUNT; ++r) {
    const char* s = rows[r];
    uint16_t text_len = strlen(s);

    int16_t baseline_x = SCREEN_W / 2;
    int16_t baseline_y = 30 + r * 90;

    uint16_t persp_idx = (uint16_t)((r * 255UL) / (ROW_COUNT - 1));
    uint32_t row_persp_q8 = FMT::get_perspective(persp_idx);

    for (uint16_t ci = 0; ci < text_len; ++ci) {
      char ch = s[ci];
      const uint8_t* fontp = font_ptr_for_char(ch);

      int16_t char_origin_x = baseline_x - (text_len * CHAR_W)/2 + ci * CHAR_W + CHAR_W/2;
      int16_t char_origin_y = baseline_y;

      uint16_t angle = (global_frame * 256 + ci * 2048);
      int32_t s_q16 = FMT::sin_q16(angle);
      int32_t c_q16 = FMT::cos_q16(angle);

      for (uint8_t col = 0; col < 5; ++col) {
        uint8_t colbyte = pgm_read_byte(fontp + col);
        for (uint8_t rowbit = 0; rowbit < 7; ++rowbit) {
          if (colbyte & (1 << rowbit)) {
            int32_t sx = (int32_t)(col - 2) << 16;
            int32_t sy = (int32_t)(rowbit - 3) << 16;

            // Rotation in Q16.16
            int32_t rx = FMT::q16_mul_s(sx, c_q16) - FMT::q16_mul_s(sy, s_q16);
            int32_t ry = FMT::q16_mul_s(sx, s_q16) + FMT::q16_mul_s(sy, c_q16);

            // Scale by perspective
            rx = FMT::q16_mul_s(rx, row_persp_q8 << 8);
            ry = FMT::q16_mul_s(ry, row_persp_q8 << 8);

            int16_t fx = char_origin_x + (int16_t)(rx >> 16);
            int16_t fy = char_origin_y + (int16_t)(ry >> 16);

            if (fx >= 0 && fx < SCREEN_W && fy >= 0 && fy < SCREEN_H) {
              tft.drawPixel(fx, fy, ILI9341_WHITE);
            }
          }
        }
      }
    }
  }
}

void setup() {
  tft.begin();
  tft.setRotation(1);
}

void loop() {
  render_frame();
  global_frame++;
}
