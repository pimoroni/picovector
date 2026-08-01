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

  void image_t::zoom(int strength) {
    rect_t bd = bounds(); int W = (int)bd.w, H = (int)bd.h;
    if(W < 1 || H < 1) return;
    image_t *src = make_scratch_image(W, H);
    if(!src) return;
    for(int y = 0; y < H; y++) memcpy(src->ptr(0, y), ptr(0, y), (size_t)W * 4);

    int fx0, fy0, fx1, fy1;
    if(!filter_rect(fx0, fy0, fx1, fy1)) { free_scratch_image(src); return; }

    int cx = W / 2, cy = H / 2;
    const int steps = 8;
    int maxdz = (strength * 110) / 255;   // Q8 max zoom-in per sample line

    for(int y = fy0; y < fy1; y++) {
      int dy0 = y - cy;
      uint8_t *out = (uint8_t*)ptr(fx0, y);
      for(int x = fx0; x < fx1; x++) {
        int dx0 = x - cx, r = 0, g = 0, b = 0;
        for(int k = 0; k < steps; k++) {
          int scale = 256 - (maxdz * k) / (steps - 1);   // 256 (1.0) .. 256-maxdz
          int sx = cx + ((dx0 * scale) >> 8);
          int sy = cy + ((dy0 * scale) >> 8);
          if(sx < 0) sx = 0; else if(sx >= W) sx = W - 1;
          if(sy < 0) sy = 0; else if(sy >= H) sy = H - 1;
          uint8_t *s = (uint8_t*)src->ptr(sx, sy);
          r += s[0]; g += s[1]; b += s[2];
        }
        out[0] = (uint8_t)(r / steps);
        out[1] = (uint8_t)(g / steps);
        out[2] = (uint8_t)(b / steps); // leave alpha
        out += 4;
      }
    }
    free_scratch_image(src);
  }

}
