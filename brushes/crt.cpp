#include "../brush.hpp"

namespace picovector {

  // See color.cpp for the brush-file layout. CRT look: darken every `spacing`-th
  // row by `darkness`, plus a rounded corner/edge falloff so the picture reads
  // like a curved tube. Position-dependent (scanlines follow y; the falloff is
  // keyed on distance from the image centre).

  static inline uint8_t clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v); }
  // rounded corner/edge darkening factor 0..255 for pixel (x,y) in a W x H image
  static inline int tube(int x, int y, int W, int H) {
    int dx = 2 * x - W; if(dx < 0) dx = -dx;              // 0 centre .. W edge
    int dy = 2 * y - H; if(dy < 0) dy = -dy;
    int nx = W ? (int)((long)dx * 255 / W) : 0;
    int ny = H ? (int)((long)dy * 255 / H) : 0;
    int corner = (nx * nx + ny * ny) >> 8;                // 0 .. ~510
    if(corner <= 210) return 255;
    int f = 255 - (corner - 210);                          // darken past the edge
    return f < 40 ? 40 : f;
  }

  crt_brush_t::crt_brush_t(int spacing, int darkness, int str)
    : spacing(spacing < 1 ? 1 : spacing), darkness(darkness),
      str(str < 0 ? 0 : (str > 256 ? 256 : str)) {}

  void crt_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    rect_t bnd = target->bounds(); int W = (int)bnd.w, H = (int)bnd.h;
    int sl = 255 - darkness;
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y;
      int line = (y % spacing) == 0 ? sl : 255;
      uint8_t *p = (uint8_t*)target->ptr(x, y);
      for(int w = spans[i].w; w; w--) {
        int f = line * tube(x, y, W, H) / 255;
        f = 255 - (((255 - f) * str) >> 8);              // scale the darkening by strength
        p[0] = (uint8_t)((p[0] * f) >> 8);
        p[1] = (uint8_t)((p[1] * f) >> 8);
        p[2] = (uint8_t)((p[2] * f) >> 8);
        p += 4; x++;
      }
    }
  }

  void crt_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    rect_t bnd = target->bounds(); int W = (int)bnd.w, H = (int)bnd.h;
    int sl = 255 - darkness;
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y;
      int line = (y % spacing) == 0 ? sl : 255;
      uint8_t *p = (uint8_t*)target->ptr(x, y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward the darkened value by coverage (leave alpha)
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        if(!m) { p += 4; x++; continue; }
        int f = line * tube(x, y, W, H) / 255;
        f = 255 - (((255 - f) * str) >> 8);              // scale the darkening by strength
        int n0 = (p[0] * f) >> 8, n1 = (p[1] * f) >> 8, n2 = (p[2] * f) >> 8;
        p[0] = (uint8_t)(p[0] + (((n0 - p[0]) * m) >> 8));
        p[1] = (uint8_t)(p[1] + (((n1 - p[1]) * m) >> 8));
        p[2] = (uint8_t)(p[2] + (((n2 - p[2]) * m) >> 8));
        p += 4; x++;
      }
    }
  }

}
