#pragma once

#include "picovector.hpp"
#include "mat3.hpp"
#include "image.hpp"
#include "blend.hpp"
#include "color.hpp"

namespace picovector {

  // Blend the shared span buffer with a brush (dispatches to its batch func).
  void _blend_spans(image_t *target, brush_t *brush);
  void _blend_masked_spans(image_t *target, brush_t *brush);

  class brush_t {
  public:
    virtual span_func_t span_func() = 0;
    virtual masked_span_func_t masked_span_func() = 0;

    // Composite a whole list of pre-clipped spans in one call: blend_spans a
    // solid batch, blend_masked_spans a coverage-masked (AA) batch. Every brush
    // implements both (they read the shared buffer via _spans()/_masked_spans()).
    virtual batch_span_func_t blend_spans() = 0;
    virtual batch_span_func_t blend_masked_spans() = 0;

    // Fold the shape's transform into the brush's own coordinate space, so a
    // brush with geometry (e.g. a gradient) moves with the shape it fills.
    // Called by render() before flushing; no-op for brushes without geometry.
    virtual void set_render_transform(mat3_t *transform) { (void)transform; }
  };

  class color_brush_t : public brush_t {
  public:
    color_t c;

    color_brush_t(const color_t& c);
    span_func_t span_func();
    masked_span_func_t masked_span_func();
    batch_span_func_t blend_spans() override;
    batch_span_func_t blend_masked_spans() override;
  };

  // Window / erase brush: lerps the destination toward a premultiplied target
  // colour by shape coverage — dst = lerp(dst, tint, coverage). The default tint
  // is fully transparent, making it a plain eraser (dst-out). Pass a colour to
  // punch a translucent "window" of that colour in a single pass, with AA edges
  // that blend against the background (an opaque tint behaves like a normal fill).
  // Zero-coverage pixels are left exactly untouched. With a transparent tint
  // this is bit-identical to a dst-out erase.
  class transparent_brush_t : public brush_t {
  public:
    uint32_t tint; // premultiplied target colour; 0 == fully transparent (erase)

    transparent_brush_t();                 // erase (fully transparent)
    transparent_brush_t(const color_t &c); // lerp destination toward colour c
    span_func_t span_func();
    masked_span_func_t masked_span_func();
    batch_span_func_t blend_spans() override;
    batch_span_func_t blend_masked_spans() override;
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
    batch_span_func_t blend_spans() override;
    batch_span_func_t blend_masked_spans() override;
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
    batch_span_func_t blend_spans() override;
    batch_span_func_t blend_masked_spans() override;
    void set_render_transform(mat3_t *transform) override;
  };

  enum gradient_type_t {
    GRADIENT_LINEAR = 0, // colour runs along the p1->p2 axis
    GRADIENT_RADIAL = 1  // colour runs outward from p1, reaching the last stop at |p2-p1|
  };



  // Mosaic brush: replaces the shape's area with the target content sampled at
  // an integer block grid (top-left of each `size`x`size` block).
  class pixelate_brush_t : public brush_t {
  public:
    int size;

    pixelate_brush_t(int size);
    span_func_t span_func();
    masked_span_func_t masked_span_func();
    batch_span_func_t blend_spans() override;
    batch_span_func_t blend_masked_spans() override;
  };


  // Box-blur brush: replaces the shape's area with a (2*radius+1) box average of
  // the target content behind it. Single-pass in place, so it reads some
  // already-written pixels (mild vertical softening); keep radius small.
  class blur_brush_t : public brush_t {
  public:
    int radius;

    blur_brush_t(int radius);
    span_func_t span_func();
    masked_span_func_t masked_span_func();
    batch_span_func_t blend_spans() override;
    batch_span_func_t blend_masked_spans() override;
  };


  // Lighten/darken brush: adds a signed amount to each RGB channel of the target
  // content behind the shape (positive lightens, negative darkens), clamped.
  class brightness_brush_t : public brush_t {
  public:
    int amount;

    brightness_brush_t(int amount);
    span_func_t span_func();
    masked_span_func_t masked_span_func();
    batch_span_func_t blend_spans() override;
    batch_span_func_t blend_masked_spans() override;
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
    batch_span_func_t blend_spans() override;
    batch_span_func_t blend_masked_spans() override;
    void set_render_transform(mat3_t *transform) override;
  };

}