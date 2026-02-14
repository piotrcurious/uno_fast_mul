#ifndef MOCK_ESP_HEAP_CAPS_H
#define MOCK_ESP_HEAP_CAPS_H
#include <stdlib.h>
#define MALLOC_CAP_DMA 0
#define MALLOC_CAP_32BIT 0
inline void* heap_caps_malloc(size_t size, int caps) { return malloc(size); }
#endif
