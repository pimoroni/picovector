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
      return w == 0 || h == 0;
    }

    bool contains(const point &p) {
      return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
    }

    rect normalise() {
      rect n = *this;

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

    rect intersection(rect r) {
      rect rn = r.normalise();
      rect tn = this->normalise();

      // Compute the edges of the intersection
      float x1 = std::max(tn.x, rn.x);
      float y1 = std::max(tn.y, rn.y);
      float x2 = std::min(tn.x + tn.w, rn.x + rn.w);
      float y2 = std::min(tn.y + tn.h, rn.y + rn.h);

      if (x1 < x2 && y1 < y2) {
        return rect(x1, y1, x2 - x1, y2 - y1);
      }

      return rect(0, 0, 0, 0);
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