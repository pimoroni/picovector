#include "../brush.hpp"

namespace picovector {

  // 1-bit threshold: each covered pixel becomes black or white by its luminance.
  // See color.cpp for the brush-file layout.

  static int onebit_level(const uint8_t *p);

  onebit_brush_t::onebit_brush_t() {}

  void onebit_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      uint8_t *p = (uint8_t*)target->ptr(spans[i].x, spans[i].y);
      for(int w = spans[i].w; w; w--) {
        int v = onebit_level(p);
        p[0] = p[1] = p[2] = (uint8_t)v; // leave alpha
        p += 4;
      }
    }
  }

  void onebit_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      uint8_t *p = (uint8_t*)target->ptr(spans[i].x, spans[i].y);
      const uint8_t *mask = spans[i].mask;
      // ease toward black/white by coverage so AA edges feather in
      for(int w = spans[i].w; w; w--) {
        int v = onebit_level(p), m = *mask++;
        p[0] = (uint8_t)(p[0] + (((v - p[0]) * m) >> 8));
        p[1] = (uint8_t)(p[1] + (((v - p[1]) * m) >> 8));
        p[2] = (uint8_t)(p[2] + (((v - p[2]) * m) >> 8));
        p += 4;
      }
    }
  }

  // ── helpers ─────────────────────────────────────────────────────────────────
  static int onebit_level(const uint8_t *p) {
    int pixel = (p[0] + p[1] * 2 + p[2]) >> 2;          // green-biased luminance
    return (pixel > 128) ? 0xff : 0x00;
  }

}
