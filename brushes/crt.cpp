#include "../brush.hpp"

namespace picovector {

  // See color.cpp for the brush-file layout. CRT look: darken every `spacing`-th
  // row by `darkness`, plus a rounded corner/edge falloff so the picture reads
  // like a curved tube. Position-dependent (scanlines follow y; the falloff is
  // keyed on distance from the image centre).

  struct tube_walk {
    int W, nysq, d, q, r;

    tube_walk(int x, int y, int W, int H) : W(W) {
      int dy = 2 * y - H; if(dy < 0) dy = -dy;
      int ny = H ? (int)((long)dy * 255 / H) : 0;
      nysq = ny * ny;
      d = 2 * x - W;
      long scaled = (long)(d < 0 ? -d : d) * 255;
      q = W ? (int)(scaled / W) : 0;
      r = W ? (int)(scaled % W) : 0;
    }

    inline int corner() const { return (q * q + nysq) >> 8; }

    inline void step() {
      int was = d;
      d += 2;
      if(!W) return;
      if(was >= 0) {
        r += 510;
        while(r >= W) { r -= W; q++; }
      } else if(d <= 0) {
        r -= 510;
        while(r < 0) { r += W; q--; }
      }
    }
  };

  static inline int crt_factor(int line, int corner, int str) {
    int t = 255;
    if(corner > 210) {
      t = 255 - (corner - 210);
      if(t < 40) t = 40;
    }
    int f = line * t / 255;
    return 255 - (((255 - f) * str) >> 8);
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
      int flat = crt_factor(line, 0, str);
      tube_walk tube(x, y, W, H);
      uint8_t *p = (uint8_t*)target->ptr(x, y);
      for(int w = spans[i].w; w; w--) {
        int corner = tube.corner();
        int f = corner > 210 ? crt_factor(line, corner, str) : flat;
        p[0] = (uint8_t)((p[0] * f) >> 8);
        p[1] = (uint8_t)((p[1] * f) >> 8);
        p[2] = (uint8_t)((p[2] * f) >> 8);
        p += 4; tube.step();
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
      int flat = crt_factor(line, 0, str);
      tube_walk tube(x, y, W, H);
      uint8_t *p = (uint8_t*)target->ptr(x, y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward the darkened value by coverage (leave alpha)
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        if(!m) { p += 4; tube.step(); continue; }
        int corner = tube.corner();
        int f = corner > 210 ? crt_factor(line, corner, str) : flat;
        int n0 = (p[0] * f) >> 8, n1 = (p[1] * f) >> 8, n2 = (p[2] * f) >> 8;
        p[0] = (uint8_t)(p[0] + (((n0 - p[0]) * m) >> 8));
        p[1] = (uint8_t)(p[1] + (((n1 - p[1]) * m) >> 8));
        p[2] = (uint8_t)(p[2] + (((n2 - p[2]) * m) >> 8));
        p += 4; tube.step();
      }
    }
  }

}
