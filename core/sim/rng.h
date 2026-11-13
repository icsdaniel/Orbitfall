#pragma once
// Seeded xoshiro128** generator. The only source of randomness allowed in core/.
// One instance lives in World; its state is part of the state hash.
#include <cstdint>

namespace orbitfall {

struct Rng {
  uint32_t s[4] = {1, 2, 3, 4};

  // Expands a 32-bit seed into the 128-bit state with splitmix64 (never all zero).
  void seed(uint32_t seedValue) {
    uint64_t z = seedValue;
    for (int i = 0; i < 4; i += 2) {
      z += 0x9E3779B97F4A7C15ull;
      uint64_t r = z;
      r = (r ^ (r >> 30)) * 0xBF58476D1CE4E5B9ull;
      r = (r ^ (r >> 27)) * 0x94D049BB133111EBull;
      r ^= r >> 31;
      s[i] = static_cast<uint32_t>(r);
      s[i + 1] = static_cast<uint32_t>(r >> 32);
    }
    if ((s[0] | s[1] | s[2] | s[3]) == 0) s[0] = 1;
  }

  uint32_t nextU32() {
    const uint32_t result = rotl(s[1] * 5u, 7) * 9u;
    const uint32_t t = s[1] << 9;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 11);
    return result;
  }

  // Uniform in [0, 1), 24 bits of precision.
  float nextFloat() { return static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f); }

  // Uniform in [lo, hi).
  float range(float lo, float hi) { return lo + (hi - lo) * nextFloat(); }

  // Uniform integer in [0, n); returns 0 when n == 0. Bias is negligible for small n.
  uint32_t nextBelow(uint32_t n) {
    return static_cast<uint32_t>((static_cast<uint64_t>(nextU32()) * n) >> 32);
  }

 private:
  static uint32_t rotl(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }
};

}  // namespace orbitfall
