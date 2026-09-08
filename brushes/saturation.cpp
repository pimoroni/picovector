#include "../brush.hpp"

namespace picovector {

  // Per-pixel colour filter brush (see monochrome.cpp for the layout). Reads the
  // target content behind the shape and rewrites it: push each channel away from
  // (factor>256) or toward (factor<256) its luminance. factor is Q8: 256 =
  // identity, 0 = full greyscale. A stored channel has 16 levels at RGBA4444, so a
  // factor near identity rounds away on most pixels.

  static inline uint8_t clamp8(int v);

  saturation_brush_t::saturation_brush_t(int amount) : factor(amount < -256 ? 0 : 256 + amount) {}

  void saturation_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      pv_px p = (pv_px)target->ptr(spans[i].x, spans[i].y);
      for(int w = spans[i].w; w; w--) {
        int lum = luminance(p);
        pv_r(p) = clamp8(lum + (((pv_r(p) - lum) * factor) >> 8));
        pv_g(p) = clamp8(lum + (((pv_g(p) - lum) * factor) >> 8));
        pv_b(p) = clamp8(lum + (((pv_b(p) - lum) * factor) >> 8)); // leave alpha
        p += PV_PX_STEP;
      }
    }
  }

  void saturation_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      pv_px p = (pv_px)target->ptr(spans[i].x, spans[i].y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward its saturated value by coverage so AA edges feather in
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        if(!m) { p += PV_PX_STEP; continue; }
        int lum = luminance(p);
        int nr = clamp8(lum + (((pv_r(p) - lum) * factor) >> 8));
        int ng = clamp8(lum + (((pv_g(p) - lum) * factor) >> 8));
        int nb = clamp8(lum + (((pv_b(p) - lum) * factor) >> 8));
        pv_r(p) = (uint8_t)(pv_r(p) + (((nr - pv_r(p)) * m) >> 8));
        pv_g(p) = (uint8_t)(pv_g(p) + (((ng - pv_g(p)) * m) >> 8));
        pv_b(p) = (uint8_t)(pv_b(p) + (((nb - pv_b(p)) * m) >> 8));
        p += PV_PX_STEP;
      }
    }
  }

  // ── helpers ─────────────────────────────────────────────────────────────────
  static inline uint8_t clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v); }

}
