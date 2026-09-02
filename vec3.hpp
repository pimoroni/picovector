#pragma once

// 3D vector types for the pico3d engine. Deliberately self-contained and free of
// any Pico SDK / picovector dependency so this header compiles on the host for
// CI and unit testing. Style mirrors picovector::vec2_t (types.hpp).

#include <cmath>

namespace picovector {

  struct vec3_t {
    float x, y, z;

    vec3_t() : x(0), y(0), z(0) {}
    vec3_t(float x, float y, float z) : x(x), y(y), z(z) {}

    vec3_t& operator+=(const vec3_t& r) { x += r.x; y += r.y; z += r.z; return *this; }
    vec3_t& operator-=(const vec3_t& r) { x -= r.x; y -= r.y; z -= r.z; return *this; }
    vec3_t& operator*=(float s)         { x *= s;   y *= s;   z *= s;   return *this; }
    vec3_t& operator/=(float s)         { x /= s;   y /= s;   z /= s;   return *this; }

    friend vec3_t operator+(vec3_t l, const vec3_t& r) { l += r; return l; }
    friend vec3_t operator-(vec3_t l, const vec3_t& r) { l -= r; return l; }
    friend vec3_t operator*(vec3_t l, float s)         { l *= s; return l; }
    friend vec3_t operator*(float s, vec3_t l)         { l *= s; return l; }
    friend vec3_t operator/(vec3_t l, float s)         { l /= s; return l; }

    vec3_t operator-() const { return vec3_t(-x, -y, -z); }

    bool operator==(const vec3_t& r) const { return x == r.x && y == r.y && z == r.z; }
    bool operator!=(const vec3_t& r) const { return !(*this == r); }

    float dot(const vec3_t& o) const { return x * o.x + y * o.y + z * o.z; }
    vec3_t cross(const vec3_t& o) const {
      return vec3_t(y * o.z - z * o.y,
                    z * o.x - x * o.z,
                    x * o.y - y * o.x);
    }

    float length_squared() const { return x * x + y * y + z * z; }
    float length() const { return sqrtf(length_squared()); }

    vec3_t normalized() const {
      float l = length();
      return l > 0.0f ? vec3_t(x / l, y / l, z / l) : vec3_t(0, 0, 0);
    }

    vec3_t lerp(const vec3_t& o, float t) const {
      return vec3_t(x + (o.x - x) * t, y + (o.y - y) * t, z + (o.z - z) * t);
    }
  };

  // Homogeneous / clip-space vector. Produced by mat4_t::operator*(vec3_t).
  struct vec4_t {
    float x, y, z, w;

    vec4_t() : x(0), y(0), z(0), w(0) {}
    vec4_t(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    vec4_t(const vec3_t& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}

    vec3_t xyz() const { return vec3_t(x, y, z); }

    // Perspective divide -> normalised device coordinates. Caller must ensure
    // w != 0 (near-plane clipping guarantees this on the raster path).
    vec3_t project() const { float iw = 1.0f / w; return vec3_t(x * iw, y * iw, z * iw); }
  };

}
