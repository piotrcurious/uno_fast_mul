/* FMT.h - Fast Math Toolkit
 * Unified header for fast fixed-point and 3D operations.
 */

#ifndef FMT_H
#define FMT_H

/**
 * Fast Math Toolkit (FMT)
 *
 * To provide custom lookup tables, define INCLUDE_TABLES with the path to your
 * generated header before including this file.
 *
 * Example:
 * #define INCLUDE_TABLES "my_tables.h"
 * #include "FMT.h"
 */

#include "FMT_Core.h"
#include "FMT_Fixed.h"
#include "FMT_Trig.h"
#include "FMT_3d.h"
#include "FMT_Utils.h"
#include "FMT_Ring.h"

// C-compatible API
#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t FMT_mul_u16_ap(uint16_t a, uint16_t b) { return FMT::mul_u16_ap(a, b); }
static inline int16_t  FMT_sin_u16(uint16_t a) { return FMT::sin_u16(a); }
static inline int16_t  FMT_cos_u16(uint16_t a) { return FMT::cos_u16(a); }

#ifdef __cplusplus
}
#endif

#endif
