#include "../brush.hpp"

namespace picovector {

  // Per-pixel colour filter brush (see monochrome.cpp for the layout). Reads the
  // target content behind the shape and rewrites it: photonegative, rgb -> 255 - rgb.

  invert_brush_t::invert_brush_t() {}

  void invert_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      uint8_t *p = (uint8_t*)target->ptr(spans[i].x, spans[i].y);
      for(int w = spans[i].w; w; w--) {
        p[0] = 255 - p[0];
        p[1] = 255 - p[1];
        p[2] = 255 - p[2]; // leave alpha
        p += 4;
      }
    }
  }

  void invert_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      uint8_t *p = (uint8_t*)target->ptr(spans[i].x, spans[i].y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward its inverse by coverage so AA edges feather in
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        int nr = 255 - p[0], ng = 255 - p[1], nb = 255 - p[2];
        p[0] = (uint8_t)(p[0] + (((nr - p[0]) * m) >> 8));
        p[1] = (uint8_t)(p[1] + (((ng - p[1]) * m) >> 8));
        p[2] = (uint8_t)(p[2] + (((nb - p[2]) * m) >> 8));
        p += 4;
      }
    }
  }

}
