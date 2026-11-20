#include "../brush.hpp"

namespace picovector {

  pixelate_brush_t::pixelate_brush_t(int size) : size(size) {}

  // Sampling the block's top-left anchor is stable in place: when the anchor
  // pixel is itself drawn it samples itself, so it never changes value, and
  // every other pixel in the block reads that same original colour.
  template <typename PX>
  static void pixelate_spans(image_t *target, int size, int i0, int i1, int step) {
    typedef typename PX::raw raw;
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      raw *dst = (raw*)target->ptr(x, y);
      int by = (y / size) * size;
      for(int j = 0; j < w; j++) {
        int bx = ((x + j) / size) * size;
        dst[j] = *(raw*)target->ptr(bx, by);
      }
    }
  }

  template <typename PX>
  static void pixelate_masked_spans(image_t *target, int size, int i0, int i1, int step) {
    typedef typename PX::raw raw;
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      uint8_t *mask = (uint8_t*)spans[i].mask;
      raw *dst = (raw*)target->ptr(x, y);
      int by = (y / size) * size;
      for(int j = 0; j < w; j++) {
        if(!mask[j]) continue;
        int bx = ((x + j) / size) * size;
        uint32_t src = PX::unpack(*(raw*)target->ptr(bx, by));
        // inlined mask_lerp(dst[j], src, mask[j]): straight per-channel coverage lerp
        uint32_t dv = PX::unpack(dst[j]);
        uint32_t m = mask[j];
        uint8_t *d = (uint8_t*)&dv;
        uint8_t *s = (uint8_t*)&src;
        uint32_t out;
        uint8_t *o = (uint8_t*)&out;
        o[0] = d[0] + (((int)s[0] - d[0]) * (int)m >> 8);
        o[1] = d[1] + (((int)s[1] - d[1]) * (int)m >> 8);
        o[2] = d[2] + (((int)s[2] - d[2]) * (int)m >> 8);
        o[3] = d[3] + (((int)s[3] - d[3]) * (int)m >> 8);
        dst[j] = PX::pack(out);
      }
    }
  }

  void pixelate_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    int s = size < 1 ? 1 : size;
    if(target->pixel_format() == RGB565) pixelate_spans<pv_565_px>(target, s, i0, i1, step);
    else                                 pixelate_spans<pv_rgba_px>(target, s, i0, i1, step);
  }

  void pixelate_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    int s = size < 1 ? 1 : size;
    if(target->pixel_format() == RGB565) pixelate_masked_spans<pv_565_px>(target, s, i0, i1, step);
    else                                 pixelate_masked_spans<pv_rgba_px>(target, s, i0, i1, step);
  }

}
