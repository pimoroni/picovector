#pragma once

#include <stdint.h>
#include <map>
#include <string>

#include "brush.hpp"
#include "shape.hpp"
#include "types.hpp"
#include "mat3.hpp"

using std::vector;
using std::pair;
using std::less;

namespace picovector {

  class image_t;

  struct pixel_font_glyph_t {
    uint32_t codepoint;
    uint16_t width;
  };

  class pixel_font_t {
  public:
    uint32_t glyph_count;
    uint32_t glyph_data_size;
    uint16_t width;
    uint16_t height;
    char name[32];

    pixel_font_glyph_t *glyphs;
    uint8_t *glyph_data;

    int glyph_index(int codepoint);

    // scale is an integer nearest-neighbour magnification (1 = native size).
    // Each glyph pixel becomes a scale x scale block; layout (advance, spacing,
    // height) scales with it so measure() and draw() stay consistent.
    //
    // `transform` maps the drawn glyphs in target space, on top of the placement
    // the caret and `scale` give them. nullptr keeps the axis-aligned blit;
    // anything else routes each glyph through the inverse-mapped sampler below,
    // which is much slower. Layout is unaffected either way - the caret advances
    // in untransformed text space, so measure() still describes the run.
    void draw(image_t *target, const char *text, int scale = 1,
              const mat3_t *transform = nullptr);
    void draw_glyph(image_t *target, const pixel_font_glyph_t *glyph, uint8_t *data, brush_t *brush, const rect_t &bounds, int x, int y, int scale = 1);
    rect_t measure(image_t *target, const char *text, int scale = 1);

    // Length-bounded variants for drawing/measuring a substring (e.g. one
    // word span) without a NUL terminator, so the text layout path stays
    // zero-copy over the source buffer. `end` is one past the last byte.
    void draw(image_t *target, const char *text, const char *end, int scale = 1,
              const mat3_t *transform = nullptr);
    rect_t measure(image_t *target, const char *text, const char *end, int scale = 1);

    // One glyph under an arbitrary affine placement mapping glyph pixels to
    // target pixels. `bounds` is the clip rect.
    void draw_glyph(image_t *target, const pixel_font_glyph_t *glyph, uint8_t *data, brush_t *brush, const rect_t &bounds, const mat3_t &placement);
  };

}