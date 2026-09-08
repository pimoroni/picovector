#include "../brush.hpp"

namespace picovector {

  static inline uint8_t clamp8(int v);

  brightness_brush_t::brightness_brush_t(int amount) : amount(amount) {}

  // add `amount` to each RGB channel of the target content (alpha untouched). A
  // stored channel has 16 levels at RGBA4444, so an `amount` below 9 rounds away.
  void brightness_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    brightness_brush_t *p = this;
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      int amt = p->amount;
      pv_px c = (pv_px)target->ptr(x, y);
      for(int j = 0; j < w; j++) {
        pv_r(c) = clamp8(pv_r(c) + amt);
        pv_g(c) = clamp8(pv_g(c) + amt);
        pv_b(c) = clamp8(pv_b(c) + amt);
        c += PV_PX_STEP; // leave alpha
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
      pv_px c = (pv_px)target->ptr(x, y);
      for(int j = 0; j < w; j++) {
        int m = mask[j];
        if(!m) { c += PV_PX_STEP; continue; }
        // ease the adjustment in by the coverage mask at the shape edge
        pv_r(c) = pv_r(c) + (((int)clamp8(pv_r(c) + amt) - pv_r(c)) * m >> 8);
        pv_g(c) = pv_g(c) + (((int)clamp8(pv_g(c) + amt) - pv_g(c)) * m >> 8);
        pv_b(c) = pv_b(c) + (((int)clamp8(pv_b(c) + amt) - pv_b(c)) * m >> 8);
        c += PV_PX_STEP;
      }
    }
  }

  // ── helpers ─────────────────────────────────────────────────────────────────
  static inline uint8_t clamp8(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v);
  }

}
