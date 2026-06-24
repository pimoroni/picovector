#pragma once

// 4x4 matrix for the pico3d engine. Conventions deliberately match picovector's
// mat3_t (mat3.hpp): elements are named vRC (row R, column C), builder methods
// mutate in place and return *this for chaining, multiply() post-multiplies
// (this = this * m), rotate*() take degrees and rotate*_radians() take radians.
//
// Vectors are treated as columns, so a transform chain built as
//   mat4().translate(...).rotate_y(...)  ==  T * R
// applies R first then T to a point: (T * R) * p.
//
// Host-clean: depends only on vec3.hpp + <cmath>/<string.h>.

#include <cmath>
#include <string.h>

#include "vec3.hpp"

namespace picovector {

  // Own pi constant so this header stays standalone (host builds) without
  // clashing with mat3.hpp's picovector::PV_PI (a constexpr var, so a #ifndef
  // guard can't see it) when both are included on the device.
  constexpr float PICO3D_PI = 3.14159265358979f;

  class mat4_t {
  public:
    // row-major naming: vRC. Storage order is irrelevant since access is by name.
    float v00, v01, v02, v03;
    float v10, v11, v12, v13;
    float v20, v21, v22, v23;
    float v30, v31, v32, v33;

    mat4_t() {
      memset(this, 0, sizeof(mat4_t));
      v00 = v11 = v22 = v33 = 1.0f;
    }

    mat4_t& multiply(const mat4_t& m) {
      mat4_t r;
      r.v00 = v00*m.v00 + v01*m.v10 + v02*m.v20 + v03*m.v30;
      r.v01 = v00*m.v01 + v01*m.v11 + v02*m.v21 + v03*m.v31;
      r.v02 = v00*m.v02 + v01*m.v12 + v02*m.v22 + v03*m.v32;
      r.v03 = v00*m.v03 + v01*m.v13 + v02*m.v23 + v03*m.v33;

      r.v10 = v10*m.v00 + v11*m.v10 + v12*m.v20 + v13*m.v30;
      r.v11 = v10*m.v01 + v11*m.v11 + v12*m.v21 + v13*m.v31;
      r.v12 = v10*m.v02 + v11*m.v12 + v12*m.v22 + v13*m.v32;
      r.v13 = v10*m.v03 + v11*m.v13 + v12*m.v23 + v13*m.v33;

      r.v20 = v20*m.v00 + v21*m.v10 + v22*m.v20 + v23*m.v30;
      r.v21 = v20*m.v01 + v21*m.v11 + v22*m.v21 + v23*m.v31;
      r.v22 = v20*m.v02 + v21*m.v12 + v22*m.v22 + v23*m.v32;
      r.v23 = v20*m.v03 + v21*m.v13 + v22*m.v23 + v23*m.v33;

      r.v30 = v30*m.v00 + v31*m.v10 + v32*m.v20 + v33*m.v30;
      r.v31 = v30*m.v01 + v31*m.v11 + v32*m.v21 + v33*m.v31;
      r.v32 = v30*m.v02 + v31*m.v12 + v32*m.v22 + v33*m.v32;
      r.v33 = v30*m.v03 + v31*m.v13 + v32*m.v23 + v33*m.v33;
      memcpy(this, &r, sizeof(mat4_t));
      return *this;
    }

    // --- builders (post-multiply onto the current matrix) --------------------

    mat4_t& translate(float x, float y, float z) {
      mat4_t t;
      t.v03 = x; t.v13 = y; t.v23 = z;
      return this->multiply(t);
    }

    mat4_t& scale(float s)                 { return this->scale(s, s, s); }
    mat4_t& scale(float x, float y, float z) {
      mat4_t s;
      s.v00 = x; s.v11 = y; s.v22 = z;
      return this->multiply(s);
    }

    mat4_t& rotate_x(float deg)         { return rotate_x_radians(deg * PICO3D_PI / 180.0f); }
    mat4_t& rotate_y(float deg)         { return rotate_y_radians(deg * PICO3D_PI / 180.0f); }
    mat4_t& rotate_z(float deg)         { return rotate_z_radians(deg * PICO3D_PI / 180.0f); }

    mat4_t& rotate_x_radians(float a) {
      mat4_t r; float c = cosf(a), s = sinf(a);
      r.v11 = c; r.v12 = -s; r.v21 = s; r.v22 = c;
      return this->multiply(r);
    }
    mat4_t& rotate_y_radians(float a) {
      mat4_t r; float c = cosf(a), s = sinf(a);
      r.v00 = c; r.v02 = s; r.v20 = -s; r.v22 = c;
      return this->multiply(r);
    }
    mat4_t& rotate_z_radians(float a) {
      mat4_t r; float c = cosf(a), s = sinf(a);
      r.v00 = c; r.v01 = -s; r.v10 = s; r.v11 = c;
      return this->multiply(r);
    }

    // --- camera / projection -------------------------------------------------

    // Right-handed perspective. Maps the view frustum to clip space with
    // w = -z_eye, so the per-pixel 1/w used for perspective-correct attribute
    // interpolation is well behaved. fovy in degrees.
    mat4_t& perspective(float fovy_deg, float aspect, float near, float far) {
      float f = 1.0f / tanf(0.5f * fovy_deg * PICO3D_PI / 180.0f);
      mat4_t p;                 // starts as identity; every other non-zero entry
      p.v00 = f / aspect;       // is overwritten below and the rest stay zero
      p.v11 = f;
      p.v22 = (far + near) / (near - far);
      p.v23 = (2.0f * far * near) / (near - far);
      p.v32 = -1.0f;
      p.v33 = 0.0f;
      return this->multiply(p);
    }

    // Right-handed look-at view matrix.
    mat4_t& look_at(const vec3_t& eye, const vec3_t& center, const vec3_t& up) {
      vec3_t f = (center - eye).normalized();
      vec3_t s = f.cross(up).normalized();
      vec3_t u = s.cross(f);
      mat4_t m;
      m.v00 = s.x; m.v01 = s.y; m.v02 = s.z; m.v03 = -s.dot(eye);
      m.v10 = u.x; m.v11 = u.y; m.v12 = u.z; m.v13 = -u.dot(eye);
      m.v20 = -f.x; m.v21 = -f.y; m.v22 = -f.z; m.v23 = f.dot(eye);
      m.v30 = 0; m.v31 = 0; m.v32 = 0; m.v33 = 1;
      return this->multiply(m);
    }

    // --- transforms ----------------------------------------------------------

    // Transform a position (implicit w = 1) -> homogeneous clip space.
    vec4_t operator*(const vec3_t& p) const {
      return vec4_t(
        v00*p.x + v01*p.y + v02*p.z + v03,
        v10*p.x + v11*p.y + v12*p.z + v13,
        v20*p.x + v21*p.y + v22*p.z + v23,
        v30*p.x + v31*p.y + v32*p.z + v33);
    }

    vec4_t operator*(const vec4_t& p) const {
      return vec4_t(
        v00*p.x + v01*p.y + v02*p.z + v03*p.w,
        v10*p.x + v11*p.y + v12*p.z + v13*p.w,
        v20*p.x + v21*p.y + v22*p.z + v23*p.w,
        v30*p.x + v31*p.y + v32*p.z + v33*p.w);
    }

    // Transform a direction (w = 0): ignores translation. For correct normal
    // transforms under non-uniform scale you'd want the inverse-transpose; for
    // rigid + uniform-scale model matrices this is correct as-is.
    vec3_t transform_direction(const vec3_t& d) const {
      return vec3_t(
        v00*d.x + v01*d.y + v02*d.z,
        v10*d.x + v11*d.y + v12*d.z,
        v20*d.x + v21*d.y + v22*d.z);
    }

    friend mat4_t operator*(mat4_t l, const mat4_t& r) { l.multiply(r); return l; }
  };

}
