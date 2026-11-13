#pragma once
// Small angle helpers shared by the systems. Radians everywhere (BRIEF §6).
#include <cmath>

namespace orbitfall {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTau = 2.0f * kPi;

// Wraps to [0, 2*pi).
inline float wrapTau(float a) {
  a = std::fmod(a, kTau);
  if (a < 0.0f) a += kTau;
  return a >= kTau ? 0.0f : a;
}

// Wraps to [-pi, pi).
inline float wrapPi(float a) { return wrapTau(a + kPi) - kPi; }

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace orbitfall
