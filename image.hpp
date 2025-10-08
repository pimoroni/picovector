#pragma once

#include <stdint.h>
#include <string>

#include "brush.hpp"
#include "shape.hpp"
#include "rect.hpp"
#include "font.hpp"
#include "matrix.hpp"

namespace picovector {

  enum antialiasing {
    OFF = 1,
    LOW = 2,
    X2 = 2,
    HIGH = 4,
    X4 = 4
  };

  class font;

  class image {
    public:
      uint32_t *p = nullptr;
      bool managed_buffer = false;

      rect bounds;
      picovector::brush *brush;
      picovector::font *font;
      int alpha = 255;
      enum antialiasing antialias = OFF;
      
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
