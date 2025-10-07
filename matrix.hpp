#pragma once

#include <cmath>

namespace picovector {

  class mat3 {
  public:    
    float v00, v10, v20, v01, v11, v21, v02, v12, v22;

    mat3() {
      memset(this, 0, sizeof(mat3));
      v00 = v11 = v22 = 1.0f;
    }
  
    mat3& rotate(float a) {
      return this->rotate_radians(a * M_PI / 180.0f);
    }

    mat3& rotate_radians(float a) {
      mat3 rotation;
      float c = cosf(a);
      float s = sinf(a);
      rotation.v00 = c; rotation.v01 = -s; rotation.v10 = s; rotation.v11 = c;
      return this->multiply(rotation);
    }

    mat3& translate(float x, float y) {
      mat3 translation;
      translation.v02 = x; translation.v12 = y;
      return this->multiply(translation);
    }

    mat3& scale(float v) {
      return this->scale(v, v);
    }

    mat3& scale(float x, float y) {
      mat3 scale;
      scale.v00 = x; scale.v11 = y;
      return this->multiply(scale);
    }

    mat3& multiply(const mat3 &m) {
      mat3 r;
      r.v00 = v00 * m.v00 + v01 * m.v10 + v02 * m.v20;
      r.v01 = v00 * m.v01 + v01 * m.v11 + v02 * m.v21;
      r.v02 = v00 * m.v02 + v01 * m.v12 + v02 * m.v22;
      r.v10 = v10 * m.v00 + v11 * m.v10 + v12 * m.v20;
      r.v11 = v10 * m.v01 + v11 * m.v11 + v12 * m.v21;
      r.v12 = v10 * m.v02 + v11 * m.v12 + v12 * m.v22;
      r.v20 = v20 * m.v00 + v21 * m.v10 + v22 * m.v20;
      r.v21 = v20 * m.v01 + v21 * m.v11 + v22 * m.v21;
      r.v22 = v20 * m.v02 + v21 * m.v12 + v22 * m.v22;
      memcpy(this, &r, sizeof(mat3));
      return *this;
    }
    
  };
  
}
