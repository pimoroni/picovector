#include "../brush.hpp"

namespace picovector {

  // See color.cpp for the brush-file layout. This is a per-pixel filter brush:
  // it reads the target content behind the shape and rewrites it, so there is no
  // pen colour - blend_spans/blend_masked_spans just transform each covered pixel.
  // Green amplify + hashed grain + centre-relative (vignette) edge darkening. The
  // grain hashes a PV_TICKS frame so it animates over time.

  static inline uint8_t clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v); }

  nightvision_brush_t::nightvision_brush_t() : frame((int)(PV_TICKS / 50)) {}

  void nightvision_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    rect_t bnd = target->bounds();
    int cx = (int)bnd.w / 2, cy = (int)bnd.h / 2;
    int maxd2 = cx * cx + cy * cy; if(maxd2 < 1) maxd2 = 1;
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y;
      pv_px p = (pv_px)target->ptr(x, y);
      for(int w = spans[i].w; w; w--) {
        int lum = luminance(p);
        int g = clamp8(lum * 2);                      // amplified green
        int r = lum / 4, b = lum / 4;                 // faint green tint
        // animated grain
        uint32_t h = (uint32_t)x*73856093u ^ (uint32_t)y*19349663u ^ (uint32_t)frame*83492791u; h ^= h>>13; h *= 0x5bd1e995u; h ^= h>>15;
        int d = (int)(h % 41) - 20; r=clamp8(r+d); g=clamp8(g+d); b=clamp8(b+d);
        // vignette
        int dx=x-cx, dy=y-cy; int dark=(int)((long)200*(dx*dx+dy*dy)/maxd2); if(dark>200)dark=200; int f=255-dark;
        r=(r*f)>>8; g=(g*f)>>8; b=(b*f)>>8;
        pv_r(p) = (uint8_t)r; pv_g(p) = (uint8_t)g; pv_b(p) = (uint8_t)b; // leave alpha
        p += PV_PX_STEP; x++;
      }
    }
  }

  void nightvision_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    rect_t bnd = target->bounds();
    int cx = (int)bnd.w / 2, cy = (int)bnd.h / 2;
    int maxd2 = cx * cx + cy * cy; if(maxd2 < 1) maxd2 = 1;
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y;
      pv_px p = (pv_px)target->ptr(x, y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward the night-vision value by coverage so AA edges feather in
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        if(!m) { p += PV_PX_STEP; x++; continue; }
        int lum = luminance(p);
        int g = clamp8(lum * 2);
        int r = lum / 4, b = lum / 4;
        uint32_t h = (uint32_t)x*73856093u ^ (uint32_t)y*19349663u ^ (uint32_t)frame*83492791u; h ^= h>>13; h *= 0x5bd1e995u; h ^= h>>15;
        int d = (int)(h % 41) - 20; r=clamp8(r+d); g=clamp8(g+d); b=clamp8(b+d);
        int dx=x-cx, dy=y-cy; int dark=(int)((long)200*(dx*dx+dy*dy)/maxd2); if(dark>200)dark=200; int f=255-dark;
        r=(r*f)>>8; g=(g*f)>>8; b=(b*f)>>8;
        pv_r(p) = (uint8_t)(pv_r(p) + (((r - pv_r(p)) * m) >> 8));
        pv_g(p) = (uint8_t)(pv_g(p) + (((g - pv_g(p)) * m) >> 8));
        pv_b(p) = (uint8_t)(pv_b(p) + (((b - pv_b(p)) * m) >> 8));
        p += PV_PX_STEP; x++;
      }
    }
  }

}
