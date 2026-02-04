#ifndef FAST_MUL_H
#define FAST_MUL_H

#include <stdint.h>

#ifdef __AVR__
#include <avr/pgmspace.h>
#else
#define PROGMEM
#define PGM_P const char *
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#endif

// ---------- Tables ----------
extern const uint8_t PROGMEM msb_table[256];
extern const uint16_t PROGMEM log2_table_q8[256];
extern const uint16_t PROGMEM exp2_table_q8[256];

// ---------- Helpers ----------

#ifdef __AVR__
static inline uint16_t read_word_progmem(const uint16_t *addr) {
  uint16_t val;
  asm volatile (
    "movw r30, %A1\n\t"
    "lpm %A0, Z+\n\t"
    "lpm %B0, Z\n\t"
    : "=r" (val)
    : "r" (addr)
    : "r30", "r31"
  );
  return val;
}

static inline uint8_t read_byte_progmem(const uint8_t *addr) {
  uint8_t val;
  asm volatile (
    "movw r30, %A1\n\t"
    "lpm %0, Z\n\t"
    : "=r" (val)
    : "r" (addr)
    : "r30", "r31"
  );
  return val;
}
#else
static inline uint16_t read_word_progmem(const uint16_t *addr) {
  return *addr;
}
static inline uint8_t read_byte_progmem(const uint8_t *addr) {
  return *addr;
}
#endif

static inline uint8_t fast_msb16(uint16_t v) {
  if (v >> 8) {
    uint8_t hi = (uint8_t)(v >> 8);
    return 8 + read_byte_progmem(&msb_table[hi]);
  } else {
    uint8_t lo = (uint8_t)(v & 0xFF);
    return read_byte_progmem(&msb_table[lo]);
  }
}

static inline void normalize_to_mant8(uint16_t v, uint8_t *mant8, int8_t *e_out) {
  if (v == 0) { *mant8 = 0; *e_out = -127; return; }
  uint8_t e = fast_msb16(v);
  uint32_t tmp = ((uint32_t)v << (15 - e)) >> 8;
  *mant8 = (uint8_t)tmp;
  *e_out = (int8_t)e;
}

static inline int32_t fast_log2_q8_8(uint16_t v) {
  if (v == 0) return -2147483647L - 1L; // INT32_MIN
  uint8_t mant8;
  int8_t e;
  normalize_to_mant8(v, &mant8, &e);
  uint16_t log_m = read_word_progmem(&log2_table_q8[mant8]);
  int32_t result = ((int32_t)(e - 7) << 8) + (int32_t)log_m;
  return result;
}

static inline uint32_t fast_exp2_from_q8_8(int32_t log_q8_8) {
  if (log_q8_8 <= -32768) return 0;
  int32_t integer = log_q8_8 >> 8;
  uint8_t frac = (uint8_t)(log_q8_8 & 0xFF);
  uint16_t exp_frac = read_word_progmem(&exp2_table_q8[frac]);
  if (integer >= 32) {
    return 0xFFFFFFFFUL;
  } else if (integer >= 8) {
    uint32_t val = (uint32_t)exp_frac << (integer - 8);
    return val;
  } else if (integer >= 0) {
    uint32_t val = (uint32_t)exp_frac >> (8 - integer);
    return val;
  } else {
    int shift = -integer;
    if (shift >= 24) return 0;
    uint32_t val = ((uint32_t)exp_frac) >> (8 + shift);
    return val;
  }
}

static inline uint32_t fast_log_mul_u16(uint16_t a, uint16_t b) {
  if (a == 0 || b == 0) return 0;
  int32_t la = fast_log2_q8_8(a);
  int32_t lb = fast_log2_q8_8(b);
  int32_t sum = la + lb;
  return fast_exp2_from_q8_8(sum);
}

#endif
