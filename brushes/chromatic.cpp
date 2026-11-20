#include "../brush.hpp"

namespace picovector {

  // See color.cpp for the brush-file layout. Chromatic aberration: each covered
  // pixel takes its red from `offset` px left, green from itself, blue from
  // `offset` px right (same row, x clamped). It reads along the row, so - like
  // blur.cpp - a span is computed into a stack temp before being written back,
  // otherwise freshly-written pixels would corrupt reads still to come.

  chromatic_brush_t::chromatic_brush_t(int offset) : offset(offset) {}

  #define CH 64

  template <typename PX>
  static inline uint32_t chroma_at(image_t *target, int x, int y, int offset, int W) {
    typedef typename PX::raw raw;
    int xr = x - offset; if(xr < 0) xr = 0; else if(xr >= W) xr = W - 1;
    int xb = x + offset; if(xb < 0) xb = 0; else if(xb >= W) xb = W - 1;
    uint32_t pr = PX::unpack(*(raw*)target->ptr(xr, y));
    uint32_t pg = PX::unpack(*(raw*)target->ptr(x, y));
    uint32_t pb = PX::unpack(*(raw*)target->ptr(xb, y));
    return (pr & 0xffu) | (pg & 0xff00ff00u) | (pb & 0xff0000u); // keeps pg's alpha
  }

  template <typename PX>
  static void chromatic_spans(image_t *target, int offset, int i0, int i1, int step) {
    typedef typename PX::raw raw;
    rect_t bnd = target->bounds(); int W = (int)bnd.w;
    const pv_span *spans = _spans();
    uint32_t tmp[CH];
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      while(w > 0) {
        int n = w < CH ? w : CH;
        for(int k = 0; k < n; k++) tmp[k] = chroma_at<PX>(target, x + k, y, offset, W);
        raw *d = (raw*)target->ptr(x, y);
        for(int k = 0; k < n; k++) d[k] = PX::pack(tmp[k]);
        x += n; w -= n;
      }
    }
  }

  template <typename PX>
  static void chromatic_masked_spans(image_t *target, int offset, int i0, int i1, int step) {
    typedef typename PX::raw raw;
    rect_t bnd = target->bounds(); int W = (int)bnd.w;
    const pv_masked_span *spans = _masked_spans();
    uint32_t tmp[CH];
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      const uint8_t *mask = spans[i].mask;
      while(w > 0) {
        int n = w < CH ? w : CH;
        for(int k = 0; k < n; k++) if(mask[k]) tmp[k] = chroma_at<PX>(target, x + k, y, offset, W);
        raw *d = (raw*)target->ptr(x, y);
        // ease each channel toward the shifted sample by coverage so AA edges feather in
        for(int k = 0; k < n; k++) {
          int m = *mask++;
          if(!m) continue;
          uint32_t v = PX::unpack(d[k]);
          uint8_t *p = (uint8_t*)&v;
          p[0] = (uint8_t)(p[0] + ((((int)(tmp[k] & 0xff)) - p[0]) * m >> 8));
          p[1] = (uint8_t)(p[1] + ((((int)((tmp[k] >> 8) & 0xff)) - p[1]) * m >> 8));
          p[2] = (uint8_t)(p[2] + ((((int)((tmp[k] >> 16) & 0xff)) - p[2]) * m >> 8));
          d[k] = PX::pack(v); // alpha kept
        }
        x += n; w -= n;
      }
    }
  }

  void chromatic_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    if(target->pixel_format() == RGB565) chromatic_spans<pv_565_px>(target, offset, i0, i1, step);
    else                                 chromatic_spans<pv_rgba_px>(target, offset, i0, i1, step);
  }

  void chromatic_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    if(target->pixel_format() == RGB565) chromatic_masked_spans<pv_565_px>(target, offset, i0, i1, step);
    else                                 chromatic_masked_spans<pv_rgba_px>(target, offset, i0, i1, step);
  }

}
