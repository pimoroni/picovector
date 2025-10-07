#pragma once

#include <stdint.h>
#include <string>

#include "brush.hpp"
#include "shape.hpp"
#include "rect.hpp"
#include "matrix.hpp"

namespace picovector {

  typedef struct {
    int8_t x, y;
  } glyph_path_point_t;

  typedef struct {
    uint16_t point_count;
    glyph_path_point_t *points;
  } glyph_path_t;

  typedef struct {
    uint16_t codepoint;
    int8_t x, y, w, h;
    int8_t advance;
    uint8_t path_count;
    glyph_path_t *paths;    
  } glyph_t;

  class font {
  public:
    int glyph_count;
    glyph_t *glyphs;
  };

}