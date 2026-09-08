#include "../brush.hpp"

namespace picovector {

  // See color.cpp for the brush-file layout. CRT look: darken every `spacing`-th
  // row by `darkness`, plus a rounded corner/edge falloff so the picture reads
  // like a curved tube. Position-dependent (scanlines follow y; the falloff is
  // keyed on distance from the image centre).
  // A stored channel has 16 levels at RGBA4444, so a `darkness` below 17 leaves
  // most pixels unchanged.

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
      pv_px p = (pv_px)target->ptr(x, y);
      for(int w = spans[i].w; w; w--) {
        int f = line * tube(x, y, W, H) / 255;
        f = 255 - (((255 - f) * str) >> 8);              // scale the darkening by strength
        pv_r(p) = (uint8_t)((pv_r(p) * f) >> 8);
        pv_g(p) = (uint8_t)((pv_g(p) * f) >> 8);
        pv_b(p) = (uint8_t)((pv_b(p) * f) >> 8);
        p += PV_PX_STEP; x++;
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
      pv_px p = (pv_px)target->ptr(x, y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward the darkened value by coverage (leave alpha)
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        if(!m) { p += PV_PX_STEP; x++; continue; }
        int f = line * tube(x, y, W, H) / 255;
        f = 255 - (((255 - f) * str) >> 8);              // scale the darkening by strength
        int n0 = (pv_r(p) * f) >> 8, n1 = (pv_g(p) * f) >> 8, n2 = (pv_b(p) * f) >> 8;
        pv_r(p) = (uint8_t)(pv_r(p) + (((n0 - pv_r(p)) * m) >> 8));
        pv_g(p) = (uint8_t)(pv_g(p) + (((n1 - pv_g(p)) * m) >> 8));
        pv_b(p) = (uint8_t)(pv_b(p) + (((n2 - pv_b(p)) * m) >> 8));
        p += PV_PX_STEP; x++;
      }
    }
  }

}
