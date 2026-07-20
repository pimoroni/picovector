#include "../brush.hpp"

namespace picovector {

  // See color.cpp for the brush-file layout. Chromatic aberration: each covered
  // pixel takes its red from `offset` px left, green from itself, blue from
  // `offset` px right (same row, x clamped). It reads along the row, so - like
  // blur.cpp - a span is computed into a stack temp before being written back,
  // otherwise freshly-written pixels would corrupt reads still to come.

  chromatic_brush_t::chromatic_brush_t(int offset) : offset(offset) {}

  #define CH 64

  static inline uint32_t chroma_at(image_t *target, int x, int y, int offset, int W, int H) {
    int xr = x - offset; if(xr < 0) xr = 0; else if(xr >= W) xr = W - 1;
    int xb = x + offset; if(xb < 0) xb = 0; else if(xb >= W) xb = W - 1;
    uint8_t *pr = (uint8_t*)target->ptr(xr, y), *pg = (uint8_t*)target->ptr(x, y), *pb = (uint8_t*)target->ptr(xb, y);
    return (uint32_t)(pr[0] | (pg[1] << 8) | (pb[2] << 16) | (pg[3] << 24)); // keeps pg's alpha
  }

  void chromatic_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    rect_t bnd = target->bounds(); int W = (int)bnd.w, H = (int)bnd.h;
    const pv_span *spans = _spans();
    uint32_t tmp[CH];
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      while(w > 0) {
        int n = w < CH ? w : CH;
        for(int k = 0; k < n; k++) tmp[k] = chroma_at(target, x + k, y, offset, W, H);
        uint8_t *p = (uint8_t*)target->ptr(x, y);
        for(int k = 0; k < n; k++) { *(uint32_t*)p = tmp[k]; p += 4; }
        x += n; w -= n;
      }
    }
  }

  void chromatic_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    rect_t bnd = target->bounds(); int W = (int)bnd.w, H = (int)bnd.h;
    const pv_masked_span *spans = _masked_spans();
    uint32_t tmp[CH];
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      const uint8_t *mask = spans[i].mask;
      while(w > 0) {
        int n = w < CH ? w : CH;
        for(int k = 0; k < n; k++) tmp[k] = chroma_at(target, x + k, y, offset, W, H);
        uint8_t *p = (uint8_t*)target->ptr(x, y);
        // ease each channel toward the shifted sample by coverage so AA edges feather in
        for(int k = 0; k < n; k++) {
          int m = *mask++;
          p[0] = (uint8_t)(p[0] + ((((int)(tmp[k] & 0xff)) - p[0]) * m >> 8));
          p[1] = (uint8_t)(p[1] + ((((int)((tmp[k] >> 8) & 0xff)) - p[1]) * m >> 8));
          p[2] = (uint8_t)(p[2] + ((((int)((tmp[k] >> 16) & 0xff)) - p[2]) * m >> 8));
          p += 4; // alpha kept
        }
        x += n; w -= n;
      }
    }
  }

}
