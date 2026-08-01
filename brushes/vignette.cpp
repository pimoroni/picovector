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
      uint8_t *p = (uint8_t*)target->ptr(spans[i].x, spans[i].y);
      for(int w = spans[i].w; w; w--) {
        int dx = x - cx, dy = y - cy;
        int dark = (int)((long)strength * (dx * dx + dy * dy) / maxd2); if(dark > 255) dark = 255;
        int f = 255 - dark;
        p[0] = (uint8_t)((p[0] * f) >> 8);
        p[1] = (uint8_t)((p[1] * f) >> 8);
        p[2] = (uint8_t)((p[2] * f) >> 8);
        p += 4; x++;
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
      uint8_t *p = (uint8_t*)target->ptr(spans[i].x, spans[i].y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward the darkened value by coverage (leave alpha)
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        if(!m) { p += 4; x++; continue; }
        int dx = x - cx, dy = y - cy;
        int dark = (int)((long)strength * (dx * dx + dy * dy) / maxd2); if(dark > 255) dark = 255;
        int f = 255 - dark;
        int new0 = (p[0] * f) >> 8;
        int new1 = (p[1] * f) >> 8;
        int new2 = (p[2] * f) >> 8;
        p[0] = (uint8_t)(p[0] + (((new0 - p[0]) * m) >> 8));
        p[1] = (uint8_t)(p[1] + (((new1 - p[1]) * m) >> 8));
        p[2] = (uint8_t)(p[2] + (((new2 - p[2]) * m) >> 8));
        p += 4; x++;
      }
    }
  }

}
