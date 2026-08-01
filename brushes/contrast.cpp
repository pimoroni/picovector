#include "../brush.hpp"

namespace picovector {

  // Per-pixel colour filter brush (see monochrome.cpp for the layout). Reads the
  // target content behind the shape and rewrites it: expand (factor>256) or
  // compress (factor<256) each channel around mid-grey (128). factor is Q8:
  // 256 = identity.

  static inline uint8_t clamp8(int v);

  contrast_brush_t::contrast_brush_t(int amount) : factor(amount < -256 ? 0 : 256 + amount) {}

  void contrast_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      uint8_t *p = (uint8_t*)target->ptr(spans[i].x, spans[i].y);
      for(int w = spans[i].w; w; w--) {
        p[0] = clamp8(128 + (((p[0] - 128) * factor) >> 8));
        p[1] = clamp8(128 + (((p[1] - 128) * factor) >> 8));
        p[2] = clamp8(128 + (((p[2] - 128) * factor) >> 8)); // leave alpha
        p += 4;
      }
    }
  }

  void contrast_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      uint8_t *p = (uint8_t*)target->ptr(spans[i].x, spans[i].y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward its contrasted value by coverage so AA edges feather in
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        if(!m) { p += 4; continue; }
        int nr = clamp8(128 + (((p[0] - 128) * factor) >> 8));
        int ng = clamp8(128 + (((p[1] - 128) * factor) >> 8));
        int nb = clamp8(128 + (((p[2] - 128) * factor) >> 8));
        p[0] = (uint8_t)(p[0] + (((nr - p[0]) * m) >> 8));
        p[1] = (uint8_t)(p[1] + (((ng - p[1]) * m) >> 8));
        p[2] = (uint8_t)(p[2] + (((nb - p[2]) * m) >> 8));
        p += 4;
      }
    }
  }

  // ── helpers ─────────────────────────────────────────────────────────────────
  static inline uint8_t clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v); }

}
