#pragma once

#include <stdint.h>
#include <algorithm>

#include "mat3.hpp"

using std::max;
using std::min;

namespace picovector {

  typedef int32_t fx16_t; // fixed point 16:16 type

  struct vec2_t {
    float x;
    float y;

    vec2_t() {}
    vec2_t(float x, float y) : x(x), y(y) {}

    bool operator==(const vec2_t &rhs) const {
      return x == rhs.x && y == rhs.y;
    }


    vec2_t& operator+=(const vec2_t& rhs) {x += rhs.x; y += rhs.y; return *this;}
    vec2_t& operator-=(const vec2_t& rhs) {x -= rhs.x; y -= rhs.y; return *this;}
    vec2_t& operator*=(const vec2_t& rhs) {x *= rhs.x; y *= rhs.y; return *this;}
    vec2_t& operator*=(const float rhs) {x *= rhs; y *= rhs; return *this;}
    vec2_t& operator/=(const vec2_t& rhs) {x /= rhs.x; y /= rhs.y; return *this;}
    vec2_t& operator/=(const float rhs) {x /= rhs; y /= rhs; return *this;}

    friend vec2_t operator+(vec2_t lhs, const vec2_t& rhs) { lhs += rhs; return lhs; }
    friend vec2_t operator-(vec2_t lhs, const vec2_t& rhs) { lhs -= rhs; return lhs; }
    friend vec2_t operator*(vec2_t lhs, const vec2_t& rhs) { lhs *= rhs; return lhs; }
    friend vec2_t operator*(vec2_t lhs, const float rhs) { lhs *= rhs; return lhs; }
    friend vec2_t operator/(vec2_t lhs, const vec2_t& rhs) { lhs /= rhs; return lhs; }
    friend vec2_t operator/(vec2_t lhs, const float rhs) { lhs /= rhs; return lhs; }

    vec2_t operator+() const { return *this; }
    vec2_t operator-() const { return vec2_t(-x, -y); }

    friend bool operator!=(const vec2_t& a, const vec2_t& b) { return !(a == b); }

    vec2_t transform(mat3_t *t) {
      if(!t) {return *this;}
      return vec2_t(
        t->v00 * x + t->v01 * y + t->v02,
        t->v10 * x + t->v11 * y + t->v12
      );
    }

    vec2_t transform(const mat3_t &t) {
      return vec2_t(
        t.v00 * x + t.v01 * y + t.v02,
        t.v10 * x + t.v11 * y + t.v12
      );
    }

    // --- common vector operations (single precision; angles in radians) -------
    float length() const { return sqrtf(x * x + y * y); }
    float length_squared() const { return x * x + y * y; }
    float dot(const vec2_t &o) const { return x * o.x + y * o.y; }
    float cross(const vec2_t &o) const { return x * o.y - y * o.x; } // 2D scalar (z of the 3D cross)
    float distance(const vec2_t &o) const { float dx = x - o.x, dy = y - o.y; return sqrtf(dx * dx + dy * dy); }
    float distance_squared(const vec2_t &o) const { float dx = x - o.x, dy = y - o.y; return dx * dx + dy * dy; }
    float angle() const { return atan2f(y, x); }                  // angle from the +x axis
    float angle_to(const vec2_t &o) const { return atan2f(cross(o), dot(o)); } // signed angle this -> o

    vec2_t normalized() const { float l = length(); return l > 0.0f ? vec2_t(x / l, y / l) : vec2_t(0.0f, 0.0f); }
    vec2_t perpendicular() const { return vec2_t(-y, x); }        // rotated +90 degrees (CCW)
    vec2_t abs() const { return vec2_t(fabsf(x), fabsf(y)); }
    vec2_t rotated(float a) const {                               // rotate CCW by `a` radians
      float c = cosf(a), s = sinf(a);
      return vec2_t(x * c - y * s, x * s + y * c);
    }
    vec2_t lerp(const vec2_t &o, float t) const {                 // linear interpolate this..o by t
      return vec2_t(x + (o.x - x) * t, y + (o.y - y) * t);
    }
    vec2_t reflect(const vec2_t &n) const {                       // reflect about unit normal n
      float d = 2.0f * dot(n);
      return vec2_t(x - n.x * d, y - n.y * d);
    }
    vec2_t clamp_length(float max_length) const {                 // shrink to max_length, keep direction
      float l = length();
      if(l > max_length && l > 0.0f) { float k = max_length / l; return vec2_t(x * k, y * k); }
      return *this;
    }
  };

  static inline fx16_t f_to_fx16(float v) {
    return fx16_t(v * 65536.0f);
  }

  struct fx16_vec2_t {
    fx16_t x;
    fx16_t y;

#ifdef PICO
    // On Pico "fx16_t" and by extension "int32_t" is type "long"
    fx16_vec2_t(int x, int y) : x(x), y(y) {}
#endif
    fx16_vec2_t(fx16_t x, fx16_t y) : x(x), y(y) {}

    fx16_vec2_t(float fx, float fy) {
      x = f_to_fx16(fx);
      y = f_to_fx16(fy);
    }

    bool operator==(const vec2_t &rhs) const {
      return x == rhs.x && y == rhs.y;
    }

    fx16_vec2_t transform(mat3_t *t) {
      if(!t) {return *this;}
      return fx16_vec2_t(
        t->v00 * x + t->v01 * y + t->v02,
        t->v10 * x + t->v11 * y + t->v12
      );
    }

    fx16_vec2_t transform(const mat3_t &t) {
      return fx16_vec2_t(
        t.v00 * x + t.v01 * y + t.v02,
        t.v10 * x + t.v11 * y + t.v12
      );
    }
  };

  struct rect_t {
  public:
    float x;
    float y;
    float w;
    float h;

    rect_t() {}

    rect_t(float x, float y, float w, float h) : x(x), y(y), w(w), h(h) {}

    rect_t(const vec2_t &p1, const vec2_t &p2) {
      x = p1.x; y = p1. y; w = p2.x - p1.x; h = p2.y - p1.y;
    }

    vec2_t tl() {return vec2_t(x, y);}
    vec2_t br() {return vec2_t(x + w, y + h);}

    void offset(vec2_t p) {
      x += p.x;
      y += p.y;
    }

    void offset(int ox, int oy) {
      x += ox;
      y += oy;
    }

    bool operator==(const rect_t &rhs) const {
      return x == rhs.x && y == rhs.y && w == rhs.w && h == rhs.h;
    }

    bool empty() {
      return w == 0 || h == 0;
    }

    bool contains(const vec2_t &p) {
      return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
    }

    bool contains(const rect_t &o) {
      return o.x >= x && o.y >= y && (o.x + o.w) <= (x + w) && (o.y + o.h) < (y + h);
    }

    rect_t normalise() {
      rect_t n = *this;

      if(n.w < 0) {
        n.x += n.w;
        n.w = -n.w;
      }

      if(n.h < 0) {
        n.y += n.h;
        n.h = -n.h;
      }

      return n;
    }

    rect_t round() {
      rect_t r;
      r.x = floorf(this->x);
      r.y = floorf(this->y);
      r.w = ceilf(this->w + this->x) - r.x;
      r.h = ceilf(this->h + this->y) - r.y;
      return r;
    }

    rect_t floor() {
      rect_t r;
      r.x = floorf(this->x);
      r.y = floorf(this->y);
      r.w = floorf(this->w + this->x) - r.x;
      r.h = floorf(this->h + this->y) - r.y;
      return r;
    }


    rect_t intersection(const rect_t &r) const {
      rect_t rn = r;
      rn.normalise();
      rect_t tn = *this;
      tn.normalise();

      // Compute the edges of the intersection
      float x1 = max(tn.x, rn.x);
      float y1 = max(tn.y, rn.y);
      float x2 = min(tn.x + tn.w, rn.x + rn.w);
      float y2 = min(tn.y + tn.h, rn.y + rn.h);

      if (x1 < x2 && y1 < y2) {
        return rect_t(x1, y1, x2 - x1, y2 - y1);
      }

      return rect_t(0, 0, 0, 0);
    }

    bool intersects(const rect_t &r) {
      rect_t i = this->intersection(r);
      return !i.empty();
    }

    void inflate(float a) {
      this->inflate(a, a, a, a);
    }

    void inflate(float left, float top, float right, float bottom) {
      x -= left;
      y -= top;
      w += left + right;
      h += top + bottom;
    }

    void deflate(float a) {
      this->deflate(a, a, a, a);
    }

    void deflate(float left, float top, float right, float bottom) {
      x += left;
      y += top;
      w -= left + right;
      h -= top + bottom;
    }

    rect_t transform(mat3_t *m) {
      vec2_t tl = vec2_t(this->x, this->y);
      vec2_t tr = vec2_t(this->x + this->w, this->y);
      vec2_t bl = vec2_t(this->x, this->y + this->h);
      vec2_t br = vec2_t(this->x + this->w, this->y + this->h);

      tl = tl.transform(m);
      tr = tr.transform(m);
      bl = bl.transform(m);
      br = br.transform(m);

      float minx = std::min(tl.x, std::min(tr.x, std::min(bl.x, br.x)));
      float miny = std::min(tl.y, std::min(tr.y, std::min(bl.y, br.y)));
      float maxx = std::max(tl.x, std::max(tr.x, std::max(bl.x, br.x)));
      float maxy = std::max(tl.y, std::max(tr.y, std::max(bl.y, br.y)));

      return rect_t(
        x = (int32_t)minx,
        y = (int32_t)miny,
        w = (int32_t)(maxx - minx),
        h = (int32_t)(maxy - miny)
      );
    }
  };

}