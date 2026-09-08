#include "../brush.hpp"

namespace picovector {

  pixelate_brush_t::pixelate_brush_t(int size) : size(size) {}

  // Sampling the block's top-left anchor is stable in place: when the anchor
  // pixel is itself drawn it samples itself, so it never changes value, and
  // every other pixel in the block reads that same original colour.
  void pixelate_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    pixelate_brush_t *p = this;
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      int size = p->size < 1 ? 1 : p->size;
      pv_store_t *dst = (pv_store_t*)target->ptr(x, y);
      int by = (y / size) * size;
      for(int j = 0; j < w; j++) {
        int bx = ((x + j) / size) * size;
        pv_store(&dst[j], pv_load((pv_store_t*)target->ptr(bx, by)));
      }
    }
  }

  void pixelate_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    pixelate_brush_t *p = this;
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      uint8_t *mask = (uint8_t*)spans[i].mask;
      int size = p->size < 1 ? 1 : p->size;
      pv_store_t *dst = (pv_store_t*)target->ptr(x, y);
      int by = (y / size) * size;
      for(int j = 0; j < w; j++) {
        if(!mask[j]) continue;
        int bx = ((x + j) / size) * size;
        uint32_t src = pv_load((pv_store_t*)target->ptr(bx, by));
        // inlined mask_lerp(dst[j], src, mask[j]): straight per-channel coverage lerp
        uint32_t dv = pv_load(&dst[j]);
        uint32_t m = mask[j];
        uint8_t *d = (uint8_t*)&dv;
        uint8_t *s = (uint8_t*)&src;
        uint32_t out;
        uint8_t *o = (uint8_t*)&out;
        o[0] = d[0] + (((int)s[0] - d[0]) * (int)m >> 8);
        o[1] = d[1] + (((int)s[1] - d[1]) * (int)m >> 8);
        o[2] = d[2] + (((int)s[2] - d[2]) * (int)m >> 8);
        o[3] = d[3] + (((int)s[3] - d[3]) * (int)m >> 8);
        pv_store(&dst[j], out);
      }
    }
  }

}
