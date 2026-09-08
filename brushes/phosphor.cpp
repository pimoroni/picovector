#include "../brush.hpp"

namespace picovector {

  // Per-pixel filter brush: scales the target content's luminance onto a single
  // `tint` colour, so bright pixels take the full tint and dark pixels stay dark.
  // image_t::phosphor follows this with CRT scanlines and a strong bloom for the
  // glow. See color.cpp for the brush-file layout.

  // punch contrast around mid-grey (~1.5x)
  static inline int punch(int l) { l = 128 + (l - 128) * 3 / 2; return l < 0 ? 0 : (l > 255 ? 255 : l); }

  phosphor_brush_t::phosphor_brush_t(const color_t &tint) : tint(tint._p) {}

  void phosphor_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    int tr = tint & 0xff, tg = (tint >> 8) & 0xff, tb = (tint >> 16) & 0xff;
    for(int i = i0; i < i1; i += step) {
      pv_px p = (pv_px)target->ptr(spans[i].x, spans[i].y);
      for(int w = spans[i].w; w; w--) {
        int lum = punch(luminance(p));
        pv_r(p) = (uint8_t)((tr * lum) / 255);
        pv_g(p) = (uint8_t)((tg * lum) / 255);
        pv_b(p) = (uint8_t)((tb * lum) / 255); // leave alpha
        p += PV_PX_STEP;
      }
    }
  }

  void phosphor_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    int tr = tint & 0xff, tg = (tint >> 8) & 0xff, tb = (tint >> 16) & 0xff;
    for(int i = i0; i < i1; i += step) {
      pv_px p = (pv_px)target->ptr(spans[i].x, spans[i].y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward the glow colour by coverage so AA edges feather in
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        if(!m) { p += PV_PX_STEP; continue; }
        int lum = punch(luminance(p));
        int nr = (tr * lum) / 255, ng = (tg * lum) / 255, nb = (tb * lum) / 255;
        pv_r(p) = (uint8_t)(pv_r(p) + (((nr - pv_r(p)) * m) >> 8));
        pv_g(p) = (uint8_t)(pv_g(p) + (((ng - pv_g(p)) * m) >> 8));
        pv_b(p) = (uint8_t)(pv_b(p) + (((nb - pv_b(p)) * m) >> 8)); // leave alpha
        p += PV_PX_STEP;
      }
    }
  }

}
