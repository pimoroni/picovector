#include "../brush.hpp"

namespace picovector {

  // See color.cpp for the brush-file layout. This is a per-pixel filter brush:
  // it reads the target content behind the shape and rewrites it, so there is no
  // pen colour - blend_spans/blend_masked_spans just transform each covered pixel.

  static int luminance(const uint8_t *p);

  monochrome_brush_t::monochrome_brush_t() {}

  void monochrome_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      uint8_t *p = (uint8_t*)target->ptr(spans[i].x, spans[i].y);
      for(int w = spans[i].w; w; w--) {
        int l = luminance(p);
        p[0] = p[1] = p[2] = (uint8_t)l; // leave alpha
        p += 4;
      }
    }
  }

  void monochrome_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      uint8_t *p = (uint8_t*)target->ptr(spans[i].x, spans[i].y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward the grey by coverage so AA edges feather in
      for(int w = spans[i].w; w; w--) {
        int l = luminance(p), m = *mask++;
        p[0] = (uint8_t)(p[0] + (((l - p[0]) * m) >> 8));
        p[1] = (uint8_t)(p[1] + (((l - p[1]) * m) >> 8));
        p[2] = (uint8_t)(p[2] + (((l - p[2]) * m) >> 8));
        p += 4;
      }
    }
  }

  // ── helpers ─────────────────────────────────────────────────────────────────
  // green-biased luminance (crude but fast), matching the old filter
  static int luminance(const uint8_t *p) { return (p[0] + p[1] * 2 + p[2]) >> 2; }

}
