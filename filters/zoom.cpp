#include <cstdint>
#include <cstring>

#include "../picovector.hpp"
#include "../image.hpp"

namespace picovector {

  // Radial zoom blur: each pixel averages several samples taken along the line
  // from the image centre, at scales stepping from 1.0 in toward the centre. That
  // smears the image into radial streaks, like a zoom. `strength` sets the streak
  // length. Samples displaced source positions, so the original is copied into a
  // scratch image and read from there. That is a whole second image: larger than
  // the working buffer, it is a heap allocation the size of the framebuffer.

  void image_t::zoom(int strength) {
    // An indexed image stores indices, not colours. See brush.cpp.
    if(_has_palette) return;
    rect_t bd = bounds(); int W = (int)bd.w, H = (int)bd.h;
    if(W < 1 || H < 1) return;
    image_t *src = make_scratch_image(W, H);
    if(!src) return;
    for(int y = 0; y < H; y++) memcpy(src->ptr(0, y), ptr(0, y), (size_t)W * sizeof(pv_store_t));

    int fx0, fy0, fx1, fy1;
    if(!filter_rect(fx0, fy0, fx1, fy1)) { free_scratch_image(src); return; }

    int cx = W / 2, cy = H / 2;
    const int steps = 8;
    int maxdz = (strength * 110) / 255;   // Q8 max zoom-in per sample line

    for(int y = fy0; y < fy1; y++) {
      int dy0 = y - cy;
      pv_px out = (pv_px)ptr(fx0, y);
      for(int x = fx0; x < fx1; x++) {
        int dx0 = x - cx, r = 0, g = 0, b = 0;
        for(int k = 0; k < steps; k++) {
          int scale = 256 - (maxdz * k) / (steps - 1);   // 256 (1.0) .. 256-maxdz
          int sx = cx + ((dx0 * scale) >> 8);
          int sy = cy + ((dy0 * scale) >> 8);
          if(sx < 0) sx = 0; else if(sx >= W) sx = W - 1;
          if(sy < 0) sy = 0; else if(sy >= H) sy = H - 1;
          pv_px s = (pv_px)src->ptr(sx, sy);
          r += pv_r(s); g += pv_g(s); b += pv_b(s);
        }
        pv_r(out) = (uint8_t)(r / steps);
        pv_g(out) = (uint8_t)(g / steps);
        pv_b(out) = (uint8_t)(b / steps); // leave alpha
        out += PV_PX_STEP;
      }
    }
    free_scratch_image(src);
  }

}
