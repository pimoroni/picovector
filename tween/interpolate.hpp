#pragma once

#include <math.h>

#include "../types.hpp"   // vec2_t, rect_t
#include "../mat3.hpp"    // mat3_t, PV_PI

// Interpolation layer for the tween system. `pv_lerp(a, b, t)` blends two
// values of the same type by the (already-eased) fraction t. Overloads cover
// the scalar and geometric types the library uses; xform_t adds a decomposed
// affine transform whose channels interpolate correctly (rotation and scale
// must not be lerped element-wise on a raw matrix).
//
// Host-clean: depends only on the math headers, so it unit-tests off-device.

namespace picovector {

  // --- scalar / geometric lerps --------------------------------------------

  inline float pv_lerp(float a, float b, float t) {
    return a + (b - a) * t;
  }

  // Integers interpolate through float and round to nearest.
  inline int32_t pv_lerp(int32_t a, int32_t b, float t) {
    return (int32_t)lroundf((float)a + ((float)b - (float)a) * t);
  }

  inline vec2_t pv_lerp(const vec2_t &a, const vec2_t &b, float t) {
    return a.lerp(b, t); // vec2_t already provides component-wise lerp
  }

  // Rects interpolate component-wise (position and size independently). rect_t
  // has no arithmetic operators, so spell it out.
  inline rect_t pv_lerp(const rect_t &a, const rect_t &b, float t) {
    return rect_t(
      a.x + (b.x - a.x) * t,
      a.y + (b.y - a.y) * t,
      a.w + (b.w - a.w) * t,
      a.h + (b.h - a.h) * t
    );
  }

  // --- decomposed affine transform (for transform-driven tweening) ---------
  //
  // A 2D transform expressed as translate / rotate / scale about an origin.
  // Interpolating these channels independently gives correct arc motion, spin
  // and scale-about-a-point — things a straight endpoint lerp cannot express.
  // Compose to a mat3_t with to_mat3() and apply it to a vec2_t / rect_t via
  // their existing .transform(m) methods.
  //
  // `origin` is the anchor point that rotation and scale pivot about — the one
  // point left fixed by them — expressed in the same space as the geometry.
  // Default (0, 0) makes it the coordinate origin.
  struct xform_t {
    vec2_t translate = vec2_t(0.0f, 0.0f);
    float  rotation  = 0.0f;               // radians, CCW
    vec2_t scale     = vec2_t(1.0f, 1.0f);
    vec2_t origin    = vec2_t(0.0f, 0.0f); // anchor for rotation / scaling

    xform_t() {}
    xform_t(vec2_t translate, float rotation, vec2_t scale, vec2_t origin = vec2_t(0.0f, 0.0f))
      : translate(translate), rotation(rotation), scale(scale), origin(origin) {}

    // Build the affine matrix:  T · origin · R · S · origin⁻¹
    // i.e. scale and rotate about `origin`, then translate.
    mat3_t to_mat3() const;

    // Recover a transform from a matrix. Assumes a well-behaved translate /
    // rotate / (non-negative-or-uniformly-flipped) scale matrix with no shear;
    // origin is reported as (0, 0). Shear, if present, is discarded.
    static xform_t decompose(const mat3_t &m);
  };

  // Per-channel interpolation. rotation takes the shortest angular path so a
  // spin from +170° to −170° crosses 20°, not 340°.
  xform_t pv_lerp(const xform_t &a, const xform_t &b, float t);

}
