#include "../brush.hpp"

namespace picovector {

  static inline uint8_t clamp8(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v);
  }

  // add `amount` to each RGB channel of the target content (alpha untouched)
  static inline __attribute__((always_inline))
  void brightness_span(image_t *target, brightness_brush_t *p, int x, int y, int w) {
    int amt = p->amount;
    uint8_t *c = (uint8_t*)target->ptr(x, y);
    for(int i = 0; i < w; i++) {
      c[0] = clamp8(c[0] + amt);
      c[1] = clamp8(c[1] + amt);
      c[2] = clamp8(c[2] + amt);
      c += 4; // leave alpha
    }
  }
  static void brightness_brush_span_func(image_t *target, brush_t *brush, int x, int y, int w) {
    brightness_span(target, (brightness_brush_t*)brush, x, y, w);
  }
  static void brightness_brush_blend_spans(image_t *target, brush_t *brush, int i0, int i1, int step) {
    brightness_brush_t *p = (brightness_brush_t*)brush;
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) brightness_span(target, p, spans[i].x, spans[i].y, spans[i].w);
  }

  static void brightness_brush_masked_span_func(image_t *target, brush_t *brush, int x, int y, int w, uint8_t *mask) {
    brightness_brush_t *p = (brightness_brush_t*)brush;
    int amt = p->amount;
    uint8_t *c = (uint8_t*)target->ptr(x, y);
    for(int i = 0; i < w; i++) {
      int m = mask[i];
      // ease the adjustment in by the coverage mask at the shape edge
      c[0] = c[0] + (((int)clamp8(c[0] + amt) - c[0]) * m >> 8);
      c[1] = c[1] + (((int)clamp8(c[1] + amt) - c[1]) * m >> 8);
      c[2] = c[2] + (((int)clamp8(c[2] + amt) - c[2]) * m >> 8);
      c += 4;
    }
  }

  brightness_brush_t::brightness_brush_t(int amount) : amount(amount) {}

  span_func_t brightness_brush_t::span_func() {
    return brightness_brush_span_func;
  }
  batch_span_func_t brightness_brush_t::blend_spans() {
    return brightness_brush_blend_spans;
  }

  masked_span_func_t brightness_brush_t::masked_span_func() {
    return brightness_brush_masked_span_func;
  }

  static void brightness_brush_blend_masked_spans(image_t *target, brush_t *brush, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step)
      brightness_brush_masked_span_func(target, brush, spans[i].x, spans[i].y, spans[i].w, (uint8_t*)spans[i].mask);
  }
  batch_span_func_t brightness_brush_t::blend_masked_spans() { return brightness_brush_blend_masked_spans; }

}
