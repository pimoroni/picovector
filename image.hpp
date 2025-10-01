#pragma once

#include <stdint.h>
#include <string>

#include "brush.hpp"
#include "shape.hpp"
#include "rect.hpp"
#include "matrix.hpp"

namespace picovector {

  class image {
    public:
      uint32_t *p = nullptr;
      bool managed_buffer = false;

      rect bounds;
      picovector::brush *brush;          
      
      size_t _rowstride; // row stride

      image(int w, int h);
      image(uint32_t *p, int w, int h);
      ~image();

      image window(rect r);
      uint32_t* ptr(int x, int y);

      uint32_t pixel(int x, int y);
      uint32_t pixel_clamped(int x, int y);

      void clear();
      void rectangle(const rect &r);
      void circle(const point &p, const int &r);
      void draw(shape *shape);
      void blit(image *t, const point p);
      void blit(image *t, rect tr);
  };

}