#pragma once

#include <stdint.h>

// RNG simple y reproducible para inicializaciones deterministas.
static inline uint64_t rng_next_u64(uint64_t *state) {
  uint64_t x = (*state == 0) ? 0x9e3779b97f4a7c15ULL : *state;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  *state = x;
  return x * 2685821657736338717ULL;
}

static inline uint32_t rng_next_u32(uint64_t *state) {
  return (uint32_t)(rng_next_u64(state) >> 32);
}
