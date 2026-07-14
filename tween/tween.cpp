#include "interpolate.hpp"

#include <cmath>

// Transform composition/decomposition and interpolation for xform_t. Kept in a
// .cpp (rather than the header) because it pulls in the trig and is not on any
// per-pixel hot path — a tween evaluates it at most once per frame.

namespace picovector {

  mat3_t xform_t::to_mat3() const {
    // Post-multiplying builders (see mat3.hpp) compose as  M = M · step, so the
    // sequence below yields  T · origin · R · S · origin⁻¹ : a point is first
    // shifted so the origin sits at (0,0), then scaled, rotated, shifted back,
    // and finally translated.
    mat3_t m;
    m.translate(translate.x, translate.y);
    m.translate(origin.x, origin.y);
    m.rotate_radians(rotation);
    m.scale(scale.x, scale.y);
    m.translate(-origin.x, -origin.y);
    return m;
  }

  xform_t xform_t::decompose(const mat3_t &m) {
    xform_t x;
    x.translate = vec2_t(m.v02, m.v12);
    x.rotation  = atan2f(m.v10, m.v00);

    float sx = sqrtf(m.v00 * m.v00 + m.v10 * m.v10);
    float sy = sqrtf(m.v01 * m.v01 + m.v11 * m.v11);

    // A negative determinant means the transform includes a reflection; fold it
    // into the y scale so R·S still reconstructs the original orientation.
    float det = m.v00 * m.v11 - m.v01 * m.v10;
    if(det < 0.0f) sy = -sy;

    x.scale = vec2_t(sx, sy);
    x.origin = vec2_t(0.0f, 0.0f);
    return x;
  }

  xform_t pv_lerp(const xform_t &a, const xform_t &b, float t) {
    // rotation follows the shortest angular path so e.g. +170° -> -170°
    // crosses 20°, not 340°.
    float delta = b.rotation - a.rotation;
    while(delta >  PV_PI) delta -= 2.0f * PV_PI;
    while(delta < -PV_PI) delta += 2.0f * PV_PI;

    return xform_t(
      pv_lerp(a.translate, b.translate, t),
      a.rotation + delta * t,
      pv_lerp(a.scale, b.scale, t),
      pv_lerp(a.origin, b.origin, t)
    );
  }

}
