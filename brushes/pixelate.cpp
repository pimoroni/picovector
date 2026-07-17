#include "../brush.hpp"

namespace picovector {

  // blend src over dst by an 8-bit coverage mask (straight per-channel lerp)
  static inline uint32_t mask_lerp(uint32_t dst, uint32_t src, uint32_t m) {
    uint8_t *d = (uint8_t*)&dst;
    uint8_t *s = (uint8_t*)&src;
    uint32_t out;
    uint8_t *o = (uint8_t*)&out;
    o[0] = d[0] + (((int)s[0] - d[0]) * (int)m >> 8);
    o[1] = d[1] + (((int)s[1] - d[1]) * (int)m >> 8);
    o[2] = d[2] + (((int)s[2] - d[2]) * (int)m >> 8);
    o[3] = d[3] + (((int)s[3] - d[3]) * (int)m >> 8);
    return out;
  }

  // Sampling the block's top-left anchor is stable in place: when the anchor
  // pixel is itself drawn it samples itself, so it never changes value, and
  // every other pixel in the block reads that same original colour.
  static inline __attribute__((always_inline))
  void pixelate_span(image_t *target, pixelate_brush_t *p, int x, int y, int w) {
    int size = p->size < 1 ? 1 : p->size;
    uint32_t *dst = (uint32_t*)target->ptr(x, y);
    int by = (y / size) * size;
    for(int i = 0; i < w; i++) {
      int bx = ((x + i) / size) * size;
      dst[i] = *(uint32_t*)target->ptr(bx, by);
    }
  }
  static void pixelate_brush_span_func(image_t *target, brush_t *brush, int x, int y, int w) {
    pixelate_span(target, (pixelate_brush_t*)brush, x, y, w);
  }
  static void pixelate_brush_blend_spans(image_t *target, brush_t *brush, int i0, int i1, int step) {
    pixelate_brush_t *p = (pixelate_brush_t*)brush;
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) pixelate_span(target, p, spans[i].x, spans[i].y, spans[i].w);
  }

  static void pixelate_brush_masked_span_func(image_t *target, brush_t *brush, int x, int y, int w, uint8_t *mask) {
    pixelate_brush_t *p = (pixelate_brush_t*)brush;
    int size = p->size < 1 ? 1 : p->size;
    uint32_t *dst = (uint32_t*)target->ptr(x, y);
    int by = (y / size) * size;
    for(int i = 0; i < w; i++) {
      int bx = ((x + i) / size) * size;
      uint32_t src = *(uint32_t*)target->ptr(bx, by);
      dst[i] = mask_lerp(dst[i], src, mask[i]);
    }
  }

  pixelate_brush_t::pixelate_brush_t(int size) : size(size) {}

  span_func_t pixelate_brush_t::span_func() {
    return pixelate_brush_span_func;
  }
  batch_span_func_t pixelate_brush_t::blend_spans() {
    return pixelate_brush_blend_spans;
  }

  masked_span_func_t pixelate_brush_t::masked_span_func() {
    return pixelate_brush_masked_span_func;
  }

  static void pixelate_brush_blend_masked_spans(image_t *target, brush_t *brush, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step)
      pixelate_brush_masked_span_func(target, brush, spans[i].x, spans[i].y, spans[i].w, (uint8_t*)spans[i].mask);
  }
  batch_span_func_t pixelate_brush_t::blend_masked_spans() { return pixelate_brush_blend_masked_spans; }

}
