#pragma once

#include <stdint.h>
#include <algorithm>

#include "point.hpp"

namespace picovector {

  class rect {
  public:
    float x;
    float y;
    float w;
    float h;
    
    rect() {}
    
    rect(float x, float y, float w, float h) : x(x), y(y), w(w), h(h) {}

    rect(const point &p1, const point &p2) {
      x = p1.x; y = p1. y; w = p2.x - p1.x; h = p2.y - p1.y;
    }

    point tl() {return point(x, y);}
    point br() {return point(x + w, y + h);}

    void offset(point p) {
      x += p.x;
      y += p.y;
    }

    void offset(int ox, int oy) {
      x += ox;
      y += oy;
    }

    bool operator==(const rect &rhs) const {
      return x == rhs.x && y == rhs.y && w == rhs.w && h == rhs.h;
    }

    bool empty() {
      return w <= 0 || h <= 0;
    }

    bool contains(const point &p) {
      return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
    }

    rect intersection(const rect &r) {
      return rect(
        std::max(x, r.x),
        std::max(y, r.y),
        std::min(x + w, r.x + r.w) - std::max(x, r.x),
        std::min(y + h, r.y + r.h) - std::max(y, r.y)
      );
    }

    bool intersects(const rect &r) {
      rect i = this->intersection(r);
      return !i.empty();
    }

    void shrink(int a) {
      x += a;
      y += a;
      w -= a + a;
      h -= a + a;
    }

    void shrink(float left, float top, float right, float bottom) {
      x += left;
      y += top;
      w -= left + right;
      h -= top + bottom;
    }

    void debug(std::string l = "?") {
      printf("%s: %f, %f (%f x %f)\n", l.c_str(), x, y, w, h);
    }
  };

}