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

  // Perceptual luminance (Rec. 601: 0.299 R + 0.587 G + 0.114 B) of an RGBA
  // pixel, 0..255. Integer weights /256; the multiplies are single-cycle on the
  // Cortex-M33 so this costs about the same as a cruder shift-only estimate.
  inline int luminance(const uint8_t *p) { return (77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8; }

  // Catmull-Rom (a = -0.5) cubic convolution weights for the four taps either
  // side of a sample point at fractional offset t. Fixed-point: t and the
  // returned weights are Q12; the weights sum to 1.0 (4096). Integer MACs are
  // cheaper than float on the M33 and skip the per-channel int<->float casts.
  inline void cubic_weights_fx(int t, int w[4]) {
    int t2 = (t * t) >> 12;
    int t3 = (t2 * t) >> 12;
    w[0] = (-t3 + 2 * t2 - t) >> 1;
    w[1] = (3 * t3 - 5 * t2 + 8192) >> 1;  // 8192 == 2.0 in Q12
    w[2] = (-3 * t3 + 4 * t2 + t) >> 1;
    w[3] = (t3 - t2) >> 1;
  }

}
