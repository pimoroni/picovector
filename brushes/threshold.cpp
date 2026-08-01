#include "../brush.hpp"

namespace picovector {

  // See color.cpp for the brush-file layout. This is a per-pixel filter brush:
  // it reads the target content behind the shape and rewrites it, so there is no
  // pen colour - blend_spans/blend_masked_spans just transform each covered pixel.

  threshold_brush_t::threshold_brush_t(int level, const color_t &lo, const color_t &hi)
    : level(level), lo(lo._p), hi(hi._p) {}

  void threshold_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      uint8_t *p = (uint8_t*)target->ptr(spans[i].x, spans[i].y);
      for(int w = spans[i].w; w; w--) {
        int lum = luminance(p);
        *(uint32_t*)p = (lum > level) ? hi : lo;
        p += 4;
      }
    }
  }

  void threshold_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      uint8_t *p = (uint8_t*)target->ptr(spans[i].x, spans[i].y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward the thresholded colour by coverage (leave alpha)
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        if(!m) { p += 4; continue; }
        uint32_t tgt = (luminance(p) > level) ? hi : lo;
        p[0] = (uint8_t)(p[0] + ((((int)(tgt & 0xff)) - p[0]) * m >> 8));
        p[1] = (uint8_t)(p[1] + ((((int)((tgt >> 8) & 0xff)) - p[1]) * m >> 8));
        p[2] = (uint8_t)(p[2] + ((((int)((tgt >> 16) & 0xff)) - p[2]) * m >> 8));
        p += 4;
      }
    }
  }

}
