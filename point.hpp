#pragma once

#include <stdint.h>
#include <algorithm>

namespace picovector {

  class point {
  public:  
    float x;
    float y;

    point() {}
    point(float x, float y) : x(x), y(y) {}

    
    bool operator==(const point &rhs) const {
      return x == rhs.x && y == rhs.y;
    }

    point transform(mat3 *t) {
      if(!t) {return *this;}
      return point(
        t->v00 * x + t->v01 * y + t->v02,
        t->v10 * x + t->v11 * y + t->v12
      );
    }

    point transform(const mat3 &t) {
      return point(
        t.v00 * x + t.v01 * y + t.v02,
        t.v10 * x + t.v11 * y + t.v12
      );
    }

  };

}
