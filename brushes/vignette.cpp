#include "../brush.hpp"

namespace picovector {

  // Vignette: darkens each covered pixel by its squared distance from the image
  // centre, scaled by `strength` (the darkening reaches `strength`/255 at the
  // corners), so the frame edges fall off toward black. Position-dependent, keyed
  // on the pixel's offset from centre. See color.cpp for the brush-file layout.

  vignette_brush_t::vignette_brush_t(int strength) : strength(strength) {}

  void vignette_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    rect_t b = target->bounds();
    int cx = (int)b.w / 2, cy = (int)b.h / 2;
    int maxd2 = cx * cx + cy * cy; if(maxd2 < 1) maxd2 = 1;
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y;
      pv_px p = (pv_px)target->ptr(spans[i].x, spans[i].y);
      for(int w = spans[i].w; w; w--) {
        int dx = x - cx, dy = y - cy;
        int dark = (int)((long)strength * (dx * dx + dy * dy) / maxd2); if(dark > 255) dark = 255;
        int f = 255 - dark;
        pv_r(p) = (uint8_t)((pv_r(p) * f) >> 8);
        pv_g(p) = (uint8_t)((pv_g(p) * f) >> 8);
        pv_b(p) = (uint8_t)((pv_b(p) * f) >> 8);
        p += PV_PX_STEP; x++;
      }
    }
  }

  void vignette_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    rect_t b = target->bounds();
    int cx = (int)b.w / 2, cy = (int)b.h / 2;
    int maxd2 = cx * cx + cy * cy; if(maxd2 < 1) maxd2 = 1;
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y;
      pv_px p = (pv_px)target->ptr(spans[i].x, spans[i].y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward the darkened value by coverage (leave alpha)
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        if(!m) { p += PV_PX_STEP; x++; continue; }
        int dx = x - cx, dy = y - cy;
        int dark = (int)((long)strength * (dx * dx + dy * dy) / maxd2); if(dark > 255) dark = 255;
        int f = 255 - dark;
        int new0 = (pv_r(p) * f) >> 8;
        int new1 = (pv_g(p) * f) >> 8;
        int new2 = (pv_b(p) * f) >> 8;
        pv_r(p) = (uint8_t)(pv_r(p) + (((new0 - pv_r(p)) * m) >> 8));
        pv_g(p) = (uint8_t)(pv_g(p) + (((new1 - pv_g(p)) * m) >> 8));
        pv_b(p) = (uint8_t)(pv_b(p) + (((new2 - pv_b(p)) * m) >> 8));
        p += PV_PX_STEP; x++;
      }
    }
  }

}
