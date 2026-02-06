#ifndef FMT_UTILS_H
#define FMT_UTILS_H

#include "FMT_Core.h"

namespace FMT {

static inline uint32_t get_perspective(uint16_t i) {
    uint32_t n = sizeof(perspective_scale_table_q8) / 2;
    if (i >= n) i = n - 1;
    return FMT_READ16(perspective_scale_table_q8, i);
}

static inline uint16_t get_stereographic(uint16_t i) {
    uint32_t n = sizeof(stereo_radial_table_q12) / 2;
    if (i >= n) i = n - 1;
    return FMT_READ16(stereo_radial_table_q12, i);
}

} // namespace FMT

#endif
