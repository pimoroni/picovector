#pragma once

// Small shared bits used across the whole component. This is a leaf header (it
// pulls in nothing of our own), so it can sit below mat3.hpp/types.hpp and be
// included anywhere without dragging the wider API along.

#include <cstdint>

namespace picovector {

  // Single-precision pi — use this instead of the double-precision M_PI from
  // <cmath> so the maths stays float throughout.
  constexpr float PV_PI = 3.14159265358979f;

  // 16:16 signed fixed-point.
  typedef int32_t fx16_t;

  // Clamp v to the inclusive range [lo, hi].
  template<typename T>
  constexpr T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

}
