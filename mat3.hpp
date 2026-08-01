#pragma once

#include <cmath>
#include <string.h>

#include "config.hpp" // PV_MAT3_PROJECTIVE, before it is defaulted below
#include "util.hpp"   // PV_PI

// A full 3x3 by default. Set PV_MAT3_PROJECTIVE to 0 to store only the top two rows,
// for builds that never need a homography: the third row is then implicitly 0, 0, 1,
// composition drops from 27 multiplies to 12, inversion reduces to a 2x2 determinant,
// and sizeof falls from 36 to 24.
//
// The renderer reads only the top two rows either way (vec2_t::transform, and the
// rasteriser's transform_points_range, which takes six coefficients), so even a
// projective matrix draws as though its third row were 0, 0, 1. Anything wanting
// perspective has to divide through by w itself before handing points over. What the
// full matrix buys is exact storage, composition and inversion of that third row.
//
// The smaller form also matters on a GC build: the boxed MicroPython object shrinks from
// 40 bytes to 28, one 32-byte GC block rather than two, and only single-block
// allocations advance the collector's free-block hint (py/gc.c, n_free == 1). Measured
// on a Tufty 2350, that is the difference between a flat allocation cost and one that
// climbs with the heap.
//
// This header includes config.hpp itself rather than relying on picovector.hpp having
// been included first: eight other headers pull mat3.hpp in directly, and a translation
// unit that reached it before the configuration would size mat3_t differently from the
// rest of the build.
#ifndef PV_MAT3_PROJECTIVE
#define PV_MAT3_PROJECTIVE 1
#endif

namespace picovector {

  // A 2D transform: nine floats, or six with an implicit 0, 0, 1 third row when
  // PV_MAT3_PROJECTIVE is 0. rotate, translate and scale never leave the affine subset,
  // so the third row only becomes anything else if a caller sets it directly; multiply
  // and inverse handle the general case when the full matrix is stored.
  class mat3_t {
  public:
    #if PV_MAT3_PROJECTIVE
    float v00, v10, v20, v01, v11, v21, v02, v12, v22;
    #else
    float v00, v10, v01, v11, v02, v12;
    #endif

    mat3_t() {
      memset(this, 0, sizeof(mat3_t));
      v00 = v11 = 1.0f;
      #if PV_MAT3_PROJECTIVE
      v22 = 1.0f;
      #endif
    }

    // Translate, rotate and scale in one step. Same result as
    // mat3_t().translate(tx, ty).rotate(degrees).scale(sx, sy), but it composes the
    // terms directly instead of building and multiplying a matrix per stage, which from
    // MicroPython is one allocation instead of four. The third row, where there is one,
    // is left as the constructor set it.
    static mat3_t trs(float tx, float ty, float degrees, float sx, float sy) {
      float a = degrees * PV_PI / 180.0f;
      float c = cosf(a);
      float s = sinf(a);
      mat3_t r;
      r.v00 = c * sx; r.v01 = -s * sy; r.v02 = tx;
      r.v10 = s * sx; r.v11 = c * sy; r.v12 = ty;
      return r;
    }

    mat3_t& rotate(float a) {
      return this->rotate_radians(a * PV_PI / 180.0f);
    }

    mat3_t& rotate_radians(float a) {
      mat3_t rotation;
      float c = cosf(a);
      float s = sinf(a);
      rotation.v00 = c; rotation.v01 = -s; rotation.v10 = s; rotation.v11 = c;
      return this->multiply(rotation);
    }

    mat3_t& translate(float x, float y) {
      mat3_t translation;
      translation.v02 = x; translation.v12 = y;
      return this->multiply(translation);
    }

    mat3_t& scale(float v) {
      return this->scale(v, v);
    }

    mat3_t& scale(float x, float y) {
      mat3_t scale;
      scale.v00 = x; scale.v11 = y;
      return this->multiply(scale);
    }

    // this = this * m.
    mat3_t& multiply(const mat3_t &m) {
      mat3_t r;
      #if PV_MAT3_PROJECTIVE
      r.v00 = v00 * m.v00 + v01 * m.v10 + v02 * m.v20;
      r.v01 = v00 * m.v01 + v01 * m.v11 + v02 * m.v21;
      r.v02 = v00 * m.v02 + v01 * m.v12 + v02 * m.v22;
      r.v10 = v10 * m.v00 + v11 * m.v10 + v12 * m.v20;
      r.v11 = v10 * m.v01 + v11 * m.v11 + v12 * m.v21;
      r.v12 = v10 * m.v02 + v11 * m.v12 + v12 * m.v22;
      r.v20 = v20 * m.v00 + v21 * m.v10 + v22 * m.v20;
      r.v21 = v20 * m.v01 + v21 * m.v11 + v22 * m.v21;
      r.v22 = v20 * m.v02 + v21 * m.v12 + v22 * m.v22;
      #else
      // Both third rows are 0, 0, 1 and so is the product's, so only the six stored
      // terms need computing: 12 multiplies where the full 3x3 takes 27.
      r.v00 = v00 * m.v00 + v01 * m.v10;
      r.v01 = v00 * m.v01 + v01 * m.v11;
      r.v02 = v00 * m.v02 + v01 * m.v12 + v02;
      r.v10 = v10 * m.v00 + v11 * m.v10;
      r.v11 = v10 * m.v01 + v11 * m.v11;
      r.v12 = v10 * m.v02 + v11 * m.v12 + v12;
      #endif
      memcpy(this, &r, sizeof(mat3_t));
      return *this;
    }

    mat3_t& inverse() {
      // Name elements for readability
      float m00 = v00, m01 = v01, m02 = v02;
      float m10 = v10, m11 = v11, m12 = v12;
      #if PV_MAT3_PROJECTIVE
      float m20 = v20, m21 = v21, m22 = v22;

      // Determinant
      float det =
          m00 * (m11 * m22 - m12 * m21) -
          m01 * (m10 * m22 - m12 * m20) +
          m02 * (m10 * m21 - m11 * m20);

      // A singular matrix has no inverse - a zero scale is the easy way to get
      // one, and an animation passing through zero does it by accident. Leave the
      // matrix alone rather than filling it with NaN, which then propagates into
      // every coordinate it touches (gradients invert a caller's transform).
      if(det == 0.0f) return *this;
      float inv_det = 1.0f / det;

      // Adjugate (transpose of cofactor matrix),
      // then multiply by 1/det to get inverse.
      this->v00 =  (m11 * m22 - m12 * m21) * inv_det;
      this->v01 = -(m01 * m22 - m02 * m21) * inv_det;
      this->v02 =  (m01 * m12 - m02 * m11) * inv_det;
      this->v10 = -(m10 * m22 - m12 * m20) * inv_det;
      this->v11 =  (m00 * m22 - m02 * m20) * inv_det;
      this->v12 = -(m00 * m12 - m02 * m10) * inv_det;
      this->v20 =  (m10 * m21 - m11 * m20) * inv_det;
      this->v21 = -(m00 * m21 - m01 * m20) * inv_det;
      this->v22 =  (m00 * m11 - m01 * m10) * inv_det;
      #else
      // Determinant of the linear part. The full 3x3 determinant reduces to this once
      // the third row is 0, 0, 1.
      float det = m00 * m11 - m01 * m10;

      // A singular matrix has no inverse - a zero scale is the easy way to get
      // one, and an animation passing through zero does it by accident. Leave the
      // matrix alone rather than filling it with NaN, which then propagates into
      // every coordinate it touches (gradients invert a caller's transform).
      if(det == 0.0f) return *this;
      float inv_det = 1.0f / det;

      // Invert the linear part, then carry the translation through it
      this->v00 =  m11 * inv_det;
      this->v01 = -m01 * inv_det;
      this->v10 = -m10 * inv_det;
      this->v11 =  m00 * inv_det;
      this->v02 = (m01 * m12 - m02 * m11) * inv_det;
      this->v12 = (m10 * m02 - m00 * m12) * inv_det;
      #endif

      return *this;
    }
  };

}
