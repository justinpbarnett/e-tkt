#pragma once

// Stand-in for the ESP-IDF's heap_caps, which a status reads the free memory
// from. There is no heap of the board's to measure here, so it says whatever
// a test sets in stubHeap(), and nothing free until one does. See Arduino.h.

#include <stddef.h>
#include <stdint.h>

#include "Arduino.h"

#define MALLOC_CAP_8BIT (1 << 2)

inline size_t heap_caps_get_free_size(uint32_t) { return stubHeap().freeBytes; }

inline size_t heap_caps_get_largest_free_block(uint32_t) {
  return stubHeap().largestFreeBlockBytes;
}
