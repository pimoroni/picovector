#include "../brush.hpp"

namespace picovector {

  // See color.cpp for the brush-file layout. This is a per-pixel filter brush:
  // it reads the target content behind the shape and rewrites it, so there is no
  // pen colour - blend_spans/blend_masked_spans just transform each covered pixel.

  monochrome_brush_t::monochrome_brush_t() {}

  void monochrome_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      pv_px p = (pv_px)target->ptr(spans[i].x, spans[i].y);
      for(int w = spans[i].w; w; w--) {
        int l = luminance(p);
        pv_r(p) = pv_g(p) = pv_b(p) = (uint8_t)l; // leave alpha
        p += PV_PX_STEP;
      }
    }
  }

  void monochrome_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      pv_px p = (pv_px)target->ptr(spans[i].x, spans[i].y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward the grey by coverage so AA edges feather in
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        if(!m) { p += PV_PX_STEP; continue; }
        int l = luminance(p);
        pv_r(p) = (uint8_t)(pv_r(p) + (((l - pv_r(p)) * m) >> 8));
        pv_g(p) = (uint8_t)(pv_g(p) + (((l - pv_g(p)) * m) >> 8));
        pv_b(p) = (uint8_t)(pv_b(p) + (((l - pv_b(p)) * m) >> 8));
        p += PV_PX_STEP;
      }
    }
  }

}
