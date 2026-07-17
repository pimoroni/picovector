#include "../brush.hpp"

namespace picovector {

  // The target's global alpha, folded into the premultiplied source colour once.
  static inline uint32_t color_src(image_t *target, brush_t *brush) {
    uint32_t src = ((color_brush_t*)brush)->c._p;
    if(target->alpha() != 255) src = _premul_mul_alpha(src, target->alpha());
    return src;
  }

  // Per-span bodies, shared by the per-span funcs (rasteriser path) and the
  // batch funcs (draw-method path) so there's a single copy of each.
  static inline __attribute__((always_inline))
  void color_span(image_t *target, uint32_t src, int x, int y, int w) {
    uint32_t *dst = (uint32_t*)target->ptr(x, y);
    while(w--) { *dst = blend_over_premul(*dst, src); dst++; }
  }
  static inline __attribute__((always_inline))
  void color_masked_span(image_t *target, uint32_t src, int x, int y, int w, const uint8_t *mask) {
    uint32_t *dst = (uint32_t*)target->ptr(x, y);
    // fold coverage into the premultiplied colour (SWAR), then composite
    while(w--) { *dst = blend_over_premul(*dst, _premul_mul_alpha(src, *mask)); dst++; mask++; }
  }

  static void color_brush_span_func(image_t *target, brush_t *brush, int x, int y, int w) {
    color_span(target, color_src(target, brush), x, y, w);
  }
  static void color_brush_masked_span_func(image_t *target, brush_t *brush, int x, int y, int w, uint8_t *mask) {
    color_masked_span(target, color_src(target, brush), x, y, w, mask);
  }

  static void color_brush_blend_spans(image_t *target, brush_t *brush, int i0, int i1, int step) {
    uint32_t src = color_src(target, brush);
    const pv_span *spans = _spans();
    if(_a(src) == 255) {
      // Opaque: a straight copy - hoists blend_over_premul's a==255 early-out
      // out of the pixel loop. This is the clear()/solid-fill fast path.
      for(int i = i0; i < i1; i += step) {
        uint32_t *dst = (uint32_t*)target->ptr(spans[i].x, spans[i].y);
        for(int w = spans[i].w; w; w--) *dst++ = src;
      }
    } else {
      for(int i = i0; i < i1; i += step) color_span(target, src, spans[i].x, spans[i].y, spans[i].w);
    }
  }
  static void color_brush_blend_masked_spans(image_t *target, brush_t *brush, int i0, int i1, int step) {
    uint32_t src = color_src(target, brush);
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) color_masked_span(target, src, spans[i].x, spans[i].y, spans[i].w, spans[i].mask);
  }

  color_brush_t::color_brush_t(const color_t& c) : c(c) {}

  span_func_t        color_brush_t::span_func()         { return color_brush_span_func; }
  masked_span_func_t color_brush_t::masked_span_func()  { return color_brush_masked_span_func; }
  batch_span_func_t  color_brush_t::blend_spans()        { return color_brush_blend_spans; }
  batch_span_func_t  color_brush_t::blend_masked_spans() { return color_brush_blend_masked_spans; }

}
