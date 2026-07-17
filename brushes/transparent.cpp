#include "../brush.hpp"

namespace picovector {

  // Full coverage: the pixel becomes the tint outright (lerp at coverage 1).
  // For the default transparent tint this writes 0 (erase); used by clear()'s slow
  // path and image_t::rectangle(). The fast clear path uses solid_fill() below.
  static inline __attribute__((always_inline))
  void transparent_span(image_t *target, transparent_brush_t *p, int x, int y, int w) {
    uint32_t *dst = (uint32_t *)target->ptr(x, y);
    uint32_t s = p->tint;
    while(w--) { *dst++ = s; }
  }
  static void transparent_brush_span_func(image_t *target, brush_t *brush, int x, int y, int w) {
    transparent_span(target, (transparent_brush_t *)brush, x, y, w);
  }
  static void transparent_brush_blend_spans(image_t *target, brush_t *brush, int i0, int i1, int step) {
    transparent_brush_t *p = (transparent_brush_t *)brush;
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) transparent_span(target, p, spans[i].x, spans[i].y, spans[i].w);
  }

  // Coverage lerp toward the tint: dst = lerp(dst, tint, coverage), premultiplied.
  // Because it blends against dst, AA edges feather into the background instead of
  // haloing (as a source-copy would). Endpoints are exact — zero coverage leaves
  // dst untouched, full coverage becomes the tint — and the middle uses the
  // two-lane SWAR weighted sum. With tint == 0 this reduces to dst*(255-cov)/256,
  // bit-identical to the previous destination-out erase.
  static void transparent_brush_masked_span_func(image_t *target, brush_t *brush, int x, int y, int w, uint8_t *mask) {
    uint32_t *dst = (uint32_t *)target->ptr(x, y);
    uint32_t S = ((transparent_brush_t *)brush)->tint;
    uint32_t Srb = S & 0x00ff00ffu, Sga = (S >> 8) & 0x00ff00ffu;
    while(w--) {
      uint32_t cov = *mask++;
      if(cov == 0u)   { dst++; continue; }     // outside the shape: leave dst exact
      if(cov == 255u) { *dst++ = S; continue; } // fully inside: becomes the tint exact
      uint32_t D = *dst;
      uint32_t inv = 255u - cov;
      uint32_t Drb = D & 0x00ff00ffu, Dga = (D >> 8) & 0x00ff00ffu;
      uint32_t rb = ((Drb * inv + Srb * cov + 0x00800080u) >> 8) & 0x00ff00ffu;
      uint32_t ga = ((Dga * inv + Sga * cov + 0x00800080u) >> 8) & 0x00ff00ffu;
      *dst++ = rb | (ga << 8);
    }
  }

  transparent_brush_t::transparent_brush_t() : tint(0) {}
  transparent_brush_t::transparent_brush_t(const color_t &c) : tint(c._p) {}

  span_func_t transparent_brush_t::span_func() {
    return transparent_brush_span_func;
  }
  batch_span_func_t transparent_brush_t::blend_spans() {
    return transparent_brush_blend_spans;
  }

  masked_span_func_t transparent_brush_t::masked_span_func() {
    return transparent_brush_masked_span_func;
  }

  static void transparent_brush_blend_masked_spans(image_t *target, brush_t *brush, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step)
      transparent_brush_masked_span_func(target, brush, spans[i].x, spans[i].y, spans[i].w, (uint8_t*)spans[i].mask);
  }
  batch_span_func_t transparent_brush_t::blend_masked_spans() { return transparent_brush_blend_masked_spans; }

  // Solid tint fill lets clear() write the buffer to the tint directly (0 for the
  // default transparent brush, a translucent/opaque word for a colour tint).
}
