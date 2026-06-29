#pragma once

#include "picovector.hpp"
#include "mat3.hpp"
#include "image.hpp"
#include "blend.hpp"
#include "color.hpp"

namespace picovector {

  class brush_t {
  public:
    virtual span_func_t span_func() = 0;
    virtual masked_span_func_t masked_span_func() = 0;

    // Fold the shape's transform into the brush's own coordinate space, so a
    // brush with geometry (e.g. a gradient) moves with the shape it fills.
    // Called by render() before flushing; no-op for brushes without geometry.
    virtual void set_render_transform(mat3_t *transform) { (void)transform; }

    // If this brush paints a single opaque colour, return true and the 32bpp
    // framebuffer word for it — lets clear() fill directly instead of per-pixel
    // blending. False for any brush that varies or blends.
    virtual bool solid_fill(uint32_t &out) { (void)out; return false; }
  };

  void color_brush_span_func(image_t *target, brush_t *brush, int x, int y, int w);
  void color_brush_masked_span_func(image_t *target, brush_t *brush, int x, int y, int w, uint8_t *mask);
  class color_brush_t : public brush_t {
  public:
    color_t c;

    color_brush_t(const color_t& c);
    span_func_t span_func();
    masked_span_func_t masked_span_func();
    bool solid_fill(uint32_t &out) override;
  };

  class pattern_brush_t : public brush_t {
  public:
    uint8_t p[8];
    color_t c1;
    color_t c2;

    pattern_brush_t(const color_t& c1, const color_t& c2, uint8_t pattern_index);
    pattern_brush_t(const color_t& c1, const color_t& c2, uint8_t *pattern);
    span_func_t span_func();
    masked_span_func_t masked_span_func();
  };

  class image_brush_t : public brush_t {
  public:
    image_t *src;
    mat3_t inverse_transform;  // device pixels -> image space (incl. shape transform)
    mat3_t base_inverse;       // device -> image for the brush's own transform only

    image_brush_t(image_t *src);
    image_brush_t(image_t *src, mat3_t *transform);
    span_func_t span_func();
    masked_span_func_t masked_span_func();
    void set_render_transform(mat3_t *transform) override;
  };

  enum gradient_type_t {
    GRADIENT_LINEAR = 0, // colour runs along the p1->p2 axis
    GRADIENT_RADIAL = 1  // colour runs outward from p1, reaching the last stop at |p2-p1|
  };

  void gradient_brush_linear_span_func(image_t *target, brush_t *brush, int x, int y, int w);
  void gradient_brush_radial_span_func(image_t *target, brush_t *brush, int x, int y, int w);
  void gradient_brush_linear_masked_span_func(image_t *target, brush_t *brush, int x, int y, int w, uint8_t *mask);
  void gradient_brush_radial_masked_span_func(image_t *target, brush_t *brush, int x, int y, int w, uint8_t *mask);

  void pixelate_brush_span_func(image_t *target, brush_t *brush, int x, int y, int w);
  void pixelate_brush_masked_span_func(image_t *target, brush_t *brush, int x, int y, int w, uint8_t *mask);

  // Mosaic brush: replaces the shape's area with the target content sampled at
  // an integer block grid (top-left of each `size`x`size` block).
  class pixelate_brush_t : public brush_t {
  public:
    int size;

    pixelate_brush_t(int size);
    span_func_t span_func();
    masked_span_func_t masked_span_func();
  };

  void blur_brush_span_func(image_t *target, brush_t *brush, int x, int y, int w);
  void blur_brush_masked_span_func(image_t *target, brush_t *brush, int x, int y, int w, uint8_t *mask);

  // Box-blur brush: replaces the shape's area with a (2*radius+1) box average of
  // the target content behind it. Single-pass in place, so it reads some
  // already-written pixels (mild vertical softening); keep radius small.
  class blur_brush_t : public brush_t {
  public:
    int radius;

    blur_brush_t(int radius);
    span_func_t span_func();
    masked_span_func_t masked_span_func();
  };

  void brightness_brush_span_func(image_t *target, brush_t *brush, int x, int y, int w);
  void brightness_brush_masked_span_func(image_t *target, brush_t *brush, int x, int y, int w, uint8_t *mask);

  // Lighten/darken brush: adds a signed amount to each RGB channel of the target
  // content behind the shape (positive lightens, negative darkens), clamped.
  class brightness_brush_t : public brush_t {
  public:
    int amount;

    brightness_brush_t(int amount);
    span_func_t span_func();
    masked_span_func_t masked_span_func();
  };

  // SVG-style linear/radial gradient. Geometry (p1, p2) lives in the gradient's
  // own coordinate space; `transform` maps that space onto device pixels (for
  // SVG objectBoundingBox the caller maps the unit square onto the shape bbox).
  class gradient_brush_t : public brush_t {
  public:
    static constexpr int max_stops = 16;

    gradient_type_t type;
    vec2_t p1, p2;             // gradient endpoints in gradient coordinate space
    mat3_t inverse_transform;  // device pixels -> gradient coordinate space (incl. shape transform)
    mat3_t base_inverse;       // device -> gradient for the brush's own transform only
    uint32_t lut[256];         // pre-multiplied packed colours sampled along the gradient

    // positions are 0..1 stop offsets, premul_colors are color_t::_p values
    gradient_brush_t(gradient_type_t type, float x1, float y1, float x2, float y2,
                     const float *positions, const uint32_t *premul_colors, int stop_count,
                     mat3_t *transform);
    span_func_t span_func();
    masked_span_func_t masked_span_func();
    void set_render_transform(mat3_t *transform) override;
  };

}