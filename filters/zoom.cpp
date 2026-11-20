#include <cstdint>
#include <cstring>

#include "../picovector.hpp"
#include "../image.hpp"

namespace picovector {

  // Radial zoom blur: each pixel averages several samples taken along the line
  // from the image centre, at scales stepping from 1.0 in toward the centre. That
  // smears the image into radial streaks, like a zoom. `strength` sets the streak
  // length. Samples displaced source positions, so the original is copied into a
  // scratch image and read from there.
  //
  // Every sample lies between its pixel and the centre, on both axes (or
  // beyond the pixel, for a negative strength), so a 565 image is filtered in
  // place instead: visiting pixels from the edges in toward the centre (or out
  // from it) means nothing is overwritten before the last pixel that reads it.

  template <typename SRC, typename DST>
  static inline void zoom_px(const image_t *src, image_t *dst, int x, int y, int cx, int cy,
                             int W, int H, int maxdz) {
    const int steps = 8;
    int dx0 = x - cx, dy0 = y - cy, r = 0, g = 0, b = 0;
    for(int k = 0; k < steps; k++) {
      int scale = 256 - (maxdz * k) / (steps - 1);   // 256 (1.0) .. 256-maxdz
      int sx = cx + ((dx0 * scale) >> 8);
      int sy = cy + ((dy0 * scale) >> 8);
      if(sx < 0) sx = 0; else if(sx >= W) sx = W - 1;
      if(sy < 0) sy = 0; else if(sy >= H) sy = H - 1;
      uint32_t s = SRC::unpack(*(const typename SRC::raw *)src->ptr(sx, sy));
      r += s & 0xff; g += (s >> 8) & 0xff; b += (s >> 16) & 0xff;
    }
    typename DST::raw *out = (typename DST::raw *)dst->ptr(x, y);
    uint32_t v = DST::unpack(*out);
    uint8_t *o = (uint8_t*)&v;
    o[0] = (uint8_t)(r / steps);
    o[1] = (uint8_t)(g / steps);
    o[2] = (uint8_t)(b / steps); // leave alpha
    *out = DST::pack(v);
  }

  // One row of the 565 in-place pass, each side of the centre column walked
  // toward it (or away from it when zooming out).
  static void zoom565_row(image_t *img, int y, int fx0, int fx1, int cx, int cy, int W, int H, int maxdz) {
    int mid = cx < fx0 ? fx0 : (cx > fx1 ? fx1 : cx);
    if(maxdz >= 0) {
      for(int x = fx0; x < mid; x++) zoom_px<pv_565_px, pv_565_px>(img, img, x, y, cx, cy, W, H, maxdz);
      for(int x = fx1 - 1; x >= mid; x--) zoom_px<pv_565_px, pv_565_px>(img, img, x, y, cx, cy, W, H, maxdz);
    } else {
      for(int x = mid - 1; x >= fx0; x--) zoom_px<pv_565_px, pv_565_px>(img, img, x, y, cx, cy, W, H, maxdz);
      for(int x = mid; x < fx1; x++) zoom_px<pv_565_px, pv_565_px>(img, img, x, y, cx, cy, W, H, maxdz);
    }
  }

  void image_t::zoom(int strength) {
    // An indexed image is one byte a pixel; this writes four. See brush.cpp.
    if(_has_palette) return;
    rect_t bd = bounds(); int W = (int)bd.w, H = (int)bd.h;
    if(W < 1 || H < 1) return;

    int fx0, fy0, fx1, fy1;
    int cx = W / 2, cy = H / 2;
    int maxdz = (strength * 110) / 255;   // Q8 max zoom-in per sample line

    if(_pixel_format == RGB565) {
      if(!filter_rect(fx0, fy0, fx1, fy1)) return;
      pv_fence_565();
      int mid = cy < fy0 ? fy0 : (cy > fy1 ? fy1 : cy);
      if(maxdz >= 0) {
        for(int y = fy0; y < mid; y++) zoom565_row(this, y, fx0, fx1, cx, cy, W, H, maxdz);
        for(int y = fy1 - 1; y >= mid; y--) zoom565_row(this, y, fx0, fx1, cx, cy, W, H, maxdz);
      } else {
        for(int y = mid - 1; y >= fy0; y--) zoom565_row(this, y, fx0, fx1, cx, cy, W, H, maxdz);
        for(int y = mid; y < fy1; y++) zoom565_row(this, y, fx0, fx1, cx, cy, W, H, maxdz);
      }
      return;
    }

    image_t *src = make_scratch_image(W, H);
    if(!src) return;
    for(int y = 0; y < H; y++) memcpy(src->ptr(0, y), ptr(0, y), (size_t)W * 4);

    if(!filter_rect(fx0, fy0, fx1, fy1)) { free_scratch_image(src); return; }

    for(int y = fy0; y < fy1; y++)
      for(int x = fx0; x < fx1; x++)
        zoom_px<pv_rgba_px, pv_rgba_px>(src, this, x, y, cx, cy, W, H, maxdz);
    free_scratch_image(src);
  }

}
