#include "../brush.hpp"

namespace picovector {

  // blend src over dst by an 8-bit coverage mask (straight per-channel lerp)
  static inline uint32_t mask_lerp(uint32_t dst, uint32_t src, uint32_t m) {
    uint8_t *d = (uint8_t*)&dst;
    uint8_t *s = (uint8_t*)&src;
    uint32_t out;
    uint8_t *o = (uint8_t*)&out;
    o[0] = d[0] + (((int)s[0] - d[0]) * (int)m >> 8);
    o[1] = d[1] + (((int)s[1] - d[1]) * (int)m >> 8);
    o[2] = d[2] + (((int)s[2] - d[2]) * (int)m >> 8);
    o[3] = d[3] + (((int)s[3] - d[3]) * (int)m >> 8);
    return out;
  }

  // Sampling the block's top-left anchor is stable in place: when the anchor
  // pixel is itself drawn it samples itself, so it never changes value, and
  // every other pixel in the block reads that same original colour.
  void pixelate_brush_span_func(image_t *target, brush_t *brush, int x, int y, int w) {
    pixelate_brush_t *p = (pixelate_brush_t*)brush;
    int size = p->size < 1 ? 1 : p->size;
    uint32_t *dst = (uint32_t*)target->ptr(x, y);
    int by = (y / size) * size;
    for(int i = 0; i < w; i++) {
      int bx = ((x + i) / size) * size;
      dst[i] = *(uint32_t*)target->ptr(bx, by);
    }
  }

  void pixelate_brush_masked_span_func(image_t *target, brush_t *brush, int x, int y, int w, uint8_t *mask) {
    pixelate_brush_t *p = (pixelate_brush_t*)brush;
    int size = p->size < 1 ? 1 : p->size;
    uint32_t *dst = (uint32_t*)target->ptr(x, y);
    int by = (y / size) * size;
    for(int i = 0; i < w; i++) {
      int bx = ((x + i) / size) * size;
      uint32_t src = *(uint32_t*)target->ptr(bx, by);
      dst[i] = mask_lerp(dst[i], src, mask[i]);
    }
  }

  pixelate_brush_t::pixelate_brush_t(int size) : size(size) {}

  span_func_t pixelate_brush_t::span_func() {
    return pixelate_brush_span_func;
  }

  masked_span_func_t pixelate_brush_t::masked_span_func() {
    return pixelate_brush_masked_span_func;
  }

}
