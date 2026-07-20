#include "../brush.hpp"

namespace picovector {

  // Ordered (4x4 Bayer) dither to a 4-level green-biased palette. See color.cpp
  // for the brush-file layout. Position-dependent: the threshold matrix is keyed
  // on (x&3, y&3) in image space, so the pattern stays screen-aligned.
  static const uint8_t dither_bayer[16] = {
      0, 136,  34, 170,
    204,  68, 238, 102,
     51, 187,  17, 153,
    255, 119, 221,  85,
  };
  static const uint8_t dither_hi[4] = { 64, 191, 191, 255 };
  static const uint8_t dither_lo[4] = {  0,  64,  64, 191 };

  static int dither_level(const uint8_t *p, int x, int y);

  dither_brush_t::dither_brush_t() {}

  void dither_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y;
      uint8_t *p = (uint8_t*)target->ptr(x, y);
      for(int w = spans[i].w; w; w--) {
        int v = dither_level(p, x, y);
        p[0] = p[1] = p[2] = (uint8_t)v; // leave alpha
        p += 4; x++;
      }
    }
  }

  void dither_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y;
      uint8_t *p = (uint8_t*)target->ptr(x, y);
      const uint8_t *mask = spans[i].mask;
      // ease toward the dithered level by coverage so AA edges feather in
      for(int w = spans[i].w; w; w--) {
        int v = dither_level(p, x, y), m = *mask++;
        p[0] = (uint8_t)(p[0] + (((v - p[0]) * m) >> 8));
        p[1] = (uint8_t)(p[1] + (((v - p[1]) * m) >> 8));
        p[2] = (uint8_t)(p[2] + (((v - p[2]) * m) >> 8));
        p += 4; x++;
      }
    }
  }

  // ── helpers ─────────────────────────────────────────────────────────────────
  static int dither_level(const uint8_t *p, int x, int y) {
    int pixel = luminance(p);
    int scale = dither_bayer[((y & 0b11) << 2) | (x & 0b11)];
    int a = dither_hi[pixel >> 6];
    int b = dither_lo[pixel >> 6];
    return (pixel > (b + ((a - b) * scale >> 8))) ? a : b;
  }

}
