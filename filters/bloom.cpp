#include <cstdint>

#include "../picovector.hpp"
#include "../image.hpp"
#include "../util.hpp"

namespace picovector {

  // Bloom: pixels brighter than `threshold` are bright-passed into a HALF-
  // resolution scratch, blurred there, then added back over the original scaled
  // by `intensity` (0..255). Working at half res cuts the blur to ~a quarter of
  // the pixels; the halo is low-frequency so the downsample/upsample is
  // invisible. `strength` scales the intensity. blur() is the separable IIR pass.

  void image_t::bloom(int threshold, int intensity, float radius, float strength) {
    intensity = (int)(intensity * strength);
    if(intensity <= 0) return;
    rect_t bd = bounds(); int W = (int)bd.w, H = (int)bd.h;
    if(W < 2 || H < 2) return;
    int hw = W / 2, hh = H / 2;
    image_t *tmp = make_scratch_image(hw, hh);
    if(!tmp) return;

    // bright pass at half res: one source texel per 2x2 block, thresholded
    for(int hy = 0; hy < hh; hy++) {
      uint8_t *srow = (uint8_t*)ptr(0, hy * 2);
      uint8_t *d = (uint8_t*)tmp->ptr(0, hy);
      for(int hx = 0; hx < hw; hx++) {
        uint8_t *s = srow + (hx * 2) * 4;
        if(luminance(s) >= threshold) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255; }
        else { *(uint32_t*)d = 0; }
        d += 4;
      }
    }

    tmp->blur(radius * 0.5f);   // half res -> half the radius for the same spread

    // add the nearest-upsampled halo back over the full-res original
    for(int y = 0; y < H; y++) {
      int hy = y >> 1; if(hy >= hh) hy = hh - 1;
      uint8_t *s = (uint8_t*)ptr(0, y);
      uint8_t *hrow = (uint8_t*)tmp->ptr(0, hy);
      for(int x = 0; x < W; x++) {
        int hx = x >> 1; if(hx >= hw) hx = hw - 1;
        uint8_t *h = hrow + hx * 4;
        int r = s[0] + ((h[0] * intensity) >> 8); if(r > 255) r = 255;
        int g = s[1] + ((h[1] * intensity) >> 8); if(g > 255) g = 255;
        int b = s[2] + ((h[2] * intensity) >> 8); if(b > 255) b = 255;
        s[0] = (uint8_t)r; s[1] = (uint8_t)g; s[2] = (uint8_t)b; // leave alpha
        s += 4;
      }
    }
    free_scratch_image(tmp);
  }

}
