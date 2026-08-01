#include <cstdint>
#include <cstring>
#include <cstdlib>

#include "../picovector.hpp"
#include "../image.hpp"
#include "../util.hpp"

namespace picovector {

  // Glowing edges (like Photoshop's Glowing Edges): a Sobel edge magnitude drives
  // brightness, the edges keep the source colour (saturation-boosted) on a black
  // field, then bloom makes them glow. `strength` scales edge sensitivity. Reads a
  // 3x3 neighbourhood, so the original is copied into a scratch image first and
  // read from there (no in-place or cross-core corruption).

  static inline uint8_t clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v); }

  void image_t::edgeglow(int strength) {
    rect_t bd = bounds(); int W = (int)bd.w, H = (int)bd.h;
    if(W < 3 || H < 3) return;
    image_t *src = make_scratch_image(W, H);
    if(!src) return;
    for(int y = 0; y < H; y++)
      memcpy(src->ptr(0, y), ptr(0, y), (size_t)W * 4);

    int fx0, fy0, fx1, fy1;
    if(!filter_rect(fx0, fy0, fx1, fy1)) { free_scratch_image(src); return; }

    for(int y = fy0; y < fy1; y++) {
      int y0 = y > 0 ? y - 1 : 0, y1 = y < H - 1 ? y + 1 : H - 1;
      uint8_t *out = (uint8_t*)ptr(fx0, y);
      for(int x = fx0; x < fx1; x++) {
        int x0 = x > 0 ? x - 1 : 0, x1 = x < W - 1 ? x + 1 : W - 1;
        // Sobel on luminance over the 3x3 neighbourhood
        int tl = luminance((uint8_t*)src->ptr(x0, y0)), tc = luminance((uint8_t*)src->ptr(x, y0)), tr = luminance((uint8_t*)src->ptr(x1, y0));
        int ml = luminance((uint8_t*)src->ptr(x0, y)),                                              mr = luminance((uint8_t*)src->ptr(x1, y));
        int bl = luminance((uint8_t*)src->ptr(x0, y1)), bc = luminance((uint8_t*)src->ptr(x, y1)), br = luminance((uint8_t*)src->ptr(x1, y1));
        int gx = (tr + 2 * mr + br) - (tl + 2 * ml + bl);
        int gy = (bl + 2 * bc + br) - (tl + 2 * tc + tr);
        int mag = ((abs(gx) + abs(gy)) * strength) >> 8;
        if(mag > 255) mag = 255;
        // edges glow in the source colour (saturation-boosted); flat areas go black
        uint8_t *s = (uint8_t*)src->ptr(x, y);
        int l = luminance(s);
        int r = clamp8(l + (s[0] - l) * 2), g = clamp8(l + (s[1] - l) * 2), b = clamp8(l + (s[2] - l) * 2);
        out[0] = (uint8_t)(r * mag / 255);
        out[1] = (uint8_t)(g * mag / 255);
        out[2] = (uint8_t)(b * mag / 255); // leave alpha
        out += 4;
      }
    }
    free_scratch_image(src);
    bloom(40, 260, 4);   // glow the bright edges
  }

}
