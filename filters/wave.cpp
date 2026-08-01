#include <cstdint>
#include <cstring>
#include <cmath>

#include "../picovector.hpp"
#include "../image.hpp"

namespace picovector {

  // Wave: sine displacement of the image, animated over time (PV_TICKS) for a
  // dream-sequence wobble. `horizontal` shifts each row sideways by a sine of y;
  // `vertical` shifts each column up/down by a sine of x (px amplitudes; 0
  // disables that axis). `strength` scales the amplitude. The amplitude is kept
  // in Q8 sub-pixel precision so it scales smoothly as strength fades (no
  // integer-step snapping). `bilinear` interpolates the displaced sample for
  // fully sub-pixel smoothness; the default (nearest) rounds to the closest
  // texel, which is cheaper. Samples the original from a scratch copy.

  void image_t::wave(int horizontal, int vertical, float strength, bool bilinear) {
    rect_t bd = bounds(); int W = (int)bd.w, H = (int)bd.h;
    if(W < 1 || H < 1) return;
    image_t *src = make_scratch_image(W, H);
    if(!src) return;
    for(int y = 0; y < H; y++)
      memcpy(src->ptr(0, y), ptr(0, y), (size_t)W * 4);

    int slut[256];   // sine, Q8 (-256..256)
    for(int i = 0; i < 256; i++) slut[i] = (int)(sinf(i * 6.2831853f / 256.0f) * 256.0f);

    // amplitudes in Q8 pixels (sub-pixel), keeping the strength scale at full precision
    int ampH = (int)(horizontal * strength * 256.0f);
    int ampV = (int)(vertical * strength * 256.0f);
    int phase = (int)((uint32_t)PV_TICKS / 12);   // animation drift
    const int freq = 6;                            // phase step per pixel (wavelength)

    int fx0, fy0, fx1, fy1;
    if(!filter_rect(fx0, fy0, fx1, fy1)) { free_scratch_image(src); return; }

    for(int y = fy0; y < fy1; y++) {
      int dxrow = ampH ? (ampH * slut[(y * freq + phase) & 0xff]) >> 8 : 0;   // Q8 px
      uint8_t *out = (uint8_t*)ptr(fx0, y);
      for(int x = fx0; x < fx1; x++) {
        int dy = ampV ? (ampV * slut[(x * freq + phase + 64) & 0xff]) >> 8 : 0;  // Q8 px
        int sxq = (x << 8) + dxrow, syq = (y << 8) + dy;
        if(bilinear) {
          int fx = sxq & 0xff, fy = syq & 0xff;
          int x0 = sxq >> 8, y0 = syq >> 8, x1 = x0 + 1, y1 = y0 + 1;
          if(x0 < 0) x0 = 0; else if(x0 >= W) x0 = W - 1;
          if(x1 < 0) x1 = 0; else if(x1 >= W) x1 = W - 1;
          if(y0 < 0) y0 = 0; else if(y0 >= H) y0 = H - 1;
          if(y1 < 0) y1 = 0; else if(y1 >= H) y1 = H - 1;
          uint8_t *p00 = (uint8_t*)src->ptr(x0, y0), *p10 = (uint8_t*)src->ptr(x1, y0);
          uint8_t *p01 = (uint8_t*)src->ptr(x0, y1), *p11 = (uint8_t*)src->ptr(x1, y1);
          for(int c = 0; c < 3; c++) {
            int top = p00[c] + (((p10[c] - p00[c]) * fx) >> 8);
            int bot = p01[c] + (((p11[c] - p01[c]) * fx) >> 8);
            out[c] = (uint8_t)(top + (((bot - top) * fy) >> 8));
          }
        } else {
          int sx = (sxq + 128) >> 8; if(sx < 0) sx = 0; else if(sx >= W) sx = W - 1;   // round to nearest
          int sy = (syq + 128) >> 8; if(sy < 0) sy = 0; else if(sy >= H) sy = H - 1;
          *(uint32_t*)out = *(uint32_t*)src->ptr(sx, sy);
        }
        out += 4; // leave alpha
      }
    }
    free_scratch_image(src);
  }

}
