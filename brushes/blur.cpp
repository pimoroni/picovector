#include "../brush.hpp"

namespace picovector {

  // blend src over dst by an 8-bit coverage mask (straight per-channel lerp)
  static inline uint32_t blur_mask_lerp(uint32_t dst, uint32_t src, uint32_t m) {
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

  // (2*radius+1)^2 box average of the target around (px, py), clamped to bounds
  static inline uint32_t box_average(image_t *target, int px, int py, int r, int W, int H) {
    int x0 = px - r < 0 ? 0 : px - r;
    int x1 = px + r >= W ? W - 1 : px + r;
    int y0 = py - r < 0 ? 0 : py - r;
    int y1 = py + r >= H ? H - 1 : py + r;

    uint32_t sr = 0, sg = 0, sb = 0, sa = 0, n = 0;
    for(int yy = y0; yy <= y1; yy++) {
      uint8_t *row = (uint8_t*)target->ptr(x0, yy);
      for(int xx = x0; xx <= x1; xx++) {
        sr += row[0]; sg += row[1]; sb += row[2]; sa += row[3];
        row += 4;
        n++;
      }
    }

    uint32_t out;
    uint8_t *o = (uint8_t*)&out;
    o[0] = (uint8_t)(sr / n);
    o[1] = (uint8_t)(sg / n);
    o[2] = (uint8_t)(sb / n);
    o[3] = (uint8_t)(sa / n);
    return out;
  }

  // tile-width chunk: compute averages into a temp before writing back so a span
  // doesn't blur with its own freshly-written pixels (horizontal feedback)
  #define BLUR_CHUNK 64

  void blur_brush_span_func(image_t *target, brush_t *brush, int x, int y, int w) {
    blur_brush_t *p = (blur_brush_t*)brush;
    int r = p->radius < 1 ? 1 : p->radius;
    rect_t b = target->bounds();
    int W = (int)b.w, H = (int)b.h;

    uint32_t tmp[BLUR_CHUNK];
    while(w > 0) {
      int n = w < BLUR_CHUNK ? w : BLUR_CHUNK;
      for(int i = 0; i < n; i++) tmp[i] = box_average(target, x + i, y, r, W, H);
      uint32_t *dst = (uint32_t*)target->ptr(x, y);
      for(int i = 0; i < n; i++) dst[i] = tmp[i];
      x += n;
      w -= n;
    }
  }

  void blur_brush_masked_span_func(image_t *target, brush_t *brush, int x, int y, int w, uint8_t *mask) {
    blur_brush_t *p = (blur_brush_t*)brush;
    int r = p->radius < 1 ? 1 : p->radius;
    rect_t b = target->bounds();
    int W = (int)b.w, H = (int)b.h;

    uint32_t tmp[BLUR_CHUNK];
    while(w > 0) {
      int n = w < BLUR_CHUNK ? w : BLUR_CHUNK;
      for(int i = 0; i < n; i++) tmp[i] = box_average(target, x + i, y, r, W, H);
      uint32_t *dst = (uint32_t*)target->ptr(x, y);
      for(int i = 0; i < n; i++) dst[i] = blur_mask_lerp(dst[i], tmp[i], mask[i]);
      x += n;
      w -= n;
      mask += n;
    }
  }

  blur_brush_t::blur_brush_t(int radius) : radius(radius) {}

  span_func_t blur_brush_t::span_func() {
    return blur_brush_span_func;
  }

  masked_span_func_t blur_brush_t::masked_span_func() {
    return blur_brush_masked_span_func;
  }

}
