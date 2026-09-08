#include "../brush.hpp"

namespace picovector {

  // Per-pixel colour filter brush (see monochrome.cpp for the layout). Reads the
  // target content behind the shape and rewrites it: photonegative, rgb -> 255 - rgb.

  invert_brush_t::invert_brush_t() {}

  void invert_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      pv_px p = (pv_px)target->ptr(spans[i].x, spans[i].y);
      for(int w = spans[i].w; w; w--) {
        pv_r(p) = 255 - pv_r(p);
        pv_g(p) = 255 - pv_g(p);
        pv_b(p) = 255 - pv_b(p); // leave alpha
        p += PV_PX_STEP;
      }
    }
  }

  void invert_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      pv_px p = (pv_px)target->ptr(spans[i].x, spans[i].y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward its inverse by coverage so AA edges feather in
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        if(!m) { p += PV_PX_STEP; continue; }
        int nr = 255 - pv_r(p), ng = 255 - pv_g(p), nb = 255 - pv_b(p);
        pv_r(p) = (uint8_t)(pv_r(p) + (((nr - pv_r(p)) * m) >> 8));
        pv_g(p) = (uint8_t)(pv_g(p) + (((ng - pv_g(p)) * m) >> 8));
        pv_b(p) = (uint8_t)(pv_b(p) + (((nb - pv_b(p)) * m) >> 8));
        p += PV_PX_STEP;
      }
    }
  }

}
