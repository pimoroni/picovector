#include "../brush.hpp"

namespace picovector {

  static inline uint8_t clamp8(int v);

  brightness_brush_t::brightness_brush_t(int amount) : amount(amount) {}

  // add `amount` to each RGB channel of the target content (alpha untouched)
  void brightness_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    brightness_brush_t *p = this;
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      int amt = p->amount;
      uint8_t *c = (uint8_t*)target->ptr(x, y);
      for(int j = 0; j < w; j++) {
        c[0] = clamp8(c[0] + amt);
        c[1] = clamp8(c[1] + amt);
        c[2] = clamp8(c[2] + amt);
        c += 4; // leave alpha
      }
    }
  }

  void brightness_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    brightness_brush_t *p = this;
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      uint8_t *mask = (uint8_t*)spans[i].mask;
      int amt = p->amount;
      uint8_t *c = (uint8_t*)target->ptr(x, y);
      for(int j = 0; j < w; j++) {
        int m = mask[j];
        if(!m) { c += 4; continue; }
        // ease the adjustment in by the coverage mask at the shape edge
        c[0] = c[0] + (((int)clamp8(c[0] + amt) - c[0]) * m >> 8);
        c[1] = c[1] + (((int)clamp8(c[1] + amt) - c[1]) * m >> 8);
        c[2] = c[2] + (((int)clamp8(c[2] + amt) - c[2]) * m >> 8);
        c += 4;
      }
    }
  }

  // ── helpers ─────────────────────────────────────────────────────────────────
  static inline uint8_t clamp8(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v);
  }

}
