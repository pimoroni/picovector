#include <cstdint>

#include "../picovector.hpp"
#include "../image.hpp"
#include "../picovector_working_buffer.h"
#include "../util.hpp"

namespace picovector {

  // Bloom: copy the pixels brighter than `threshold` into a scratch buffer, blur
  // that copy by `radius`, then add it back over the original scaled by
  // `intensity` (0..255). Bright areas keep their detail but gain a soft halo.
  // The scratch is the shared working buffer when the image fits, else a managed
  // temp. blur() is the existing separable IIR pass.

  static void bloom_pass(image_t *src, image_t *tmp, int threshold, int intensity,
                         float radius, int W, int H) {
    // bright pass -> tmp (dark pixels become transparent black)
    for(int y = 0; y < H; y++) {
      uint8_t *s = (uint8_t*)src->ptr(0, y), *d = (uint8_t*)tmp->ptr(0, y);
      for(int x = 0; x < W; x++) {
        int lum = luminance(s);
        if(lum >= threshold) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255; }
        else { *(uint32_t*)d = 0; }
        s += 4; d += 4;
      }
    }
    tmp->blur(radius);
    // add the blurred halo back over the original
    for(int y = 0; y < H; y++) {
      uint8_t *s = (uint8_t*)src->ptr(0, y), *d = (uint8_t*)tmp->ptr(0, y);
      for(int x = 0; x < W; x++) {
        int r = s[0] + ((d[0] * intensity) >> 8); if(r > 255) r = 255;
        int g = s[1] + ((d[1] * intensity) >> 8); if(g > 255) g = 255;
        int b = s[2] + ((d[2] * intensity) >> 8); if(b > 255) b = 255;
        s[0] = (uint8_t)r; s[1] = (uint8_t)g; s[2] = (uint8_t)b;
        s += 4; d += 4;
      }
    }
  }

  void image_t::bloom(int threshold, int intensity, float radius) {
    rect_t bd = bounds(); int W = (int)bd.w, H = (int)bd.h;
    image_t *tmp = make_scratch_image(W, H);
    if(!tmp) return;
    bloom_pass(this, tmp, threshold, intensity, radius, W, H);
    free_scratch_image(tmp);
  }

}
