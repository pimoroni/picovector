#pragma once

#include <stdint.h>
#include <string>

#include "brush.hpp"
#include "shape.hpp"
#include "rect.hpp"
#include "point.hpp"
#include "matrix.hpp"

namespace picovector {

  class image;

  class glyph_path_point {
  public:
    int8_t x, y;
    
    point transform(mat3 *transform);
  };

  class glyph_path {
  public:
    uint16_t point_count;
    glyph_path_point *points;
  };

  class glyph {
  public:
    uint16_t codepoint;
    int8_t x, y, w, h;
    int8_t advance;
    uint8_t path_count;
    glyph_path *paths;   
    
    rect bounds(mat3 *transform);
  };

  class font {
  public:
    int glyph_count;
    glyph *glyphs;

    void draw(image *target, const char *text, float x, float y, float size);
    rect measure(image *target, const char *text, float size);
  };

}