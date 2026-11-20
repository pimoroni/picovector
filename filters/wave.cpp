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
  // texel, which is cheaper. Samples the original from a scratch copy. A 565
  // image only needs the rows within the vertical amplitude of the one being
  // written, so it unpacks into a ring of RGBA rows instead of copying the
  // whole image.

  // Row sources: the whole-image copy, and the ring (original row y in slot
  // y % n).
  struct wave_copy_t {
    image_t *src;
    inline const uint8_t *row(int y) const { return (const uint8_t*)src->ptr(0, y); }
  };
  struct wave_ring_t {
    image_t *src; int n;
    inline const uint8_t *row(int y) const { return (const uint8_t*)src->ptr(0, y % n); }
  };

  template <typename PX, typename ROWS>
  static void wave_row(const ROWS &rows, typename PX::raw *out, int y, int fx0, int fx1, int W, int H,
                       int dxrow, int ampV, int phase, int freq, const int *slut, bool bilinear) {
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
        const uint8_t *r0 = rows.row(y0), *r1 = rows.row(y1);
        const uint8_t *p00 = r0 + x0 * 4, *p10 = r0 + x1 * 4;
        const uint8_t *p01 = r1 + x0 * 4, *p11 = r1 + x1 * 4;
        uint32_t v = PX::unpack(*out);
        uint8_t *o = (uint8_t*)&v;
        for(int c = 0; c < 3; c++) {
          int top = p00[c] + (((p10[c] - p00[c]) * fx) >> 8);
          int bot = p01[c] + (((p11[c] - p01[c]) * fx) >> 8);
          o[c] = (uint8_t)(top + (((bot - top) * fy) >> 8));
        }
        *out = PX::pack(v);
      } else {
        int sx = (sxq + 128) >> 8; if(sx < 0) sx = 0; else if(sx >= W) sx = W - 1;   // round to nearest
        int sy = (syq + 128) >> 8; if(sy < 0) sy = 0; else if(sy >= H) sy = H - 1;
        *out = PX::pack(*(const uint32_t*)(rows.row(sy) + sx * 4));
      }
      out++; // leave alpha
    }
  }

  void image_t::wave(int horizontal, int vertical, float strength, bool bilinear) {
    // An indexed image is one byte a pixel; this writes four. See brush.cpp.
    if(_has_palette) return;
    rect_t bd = bounds(); int W = (int)bd.w, H = (int)bd.h;
    if(W < 1 || H < 1) return;

    int slut[256];   // sine, Q8 (-256..256)
    for(int i = 0; i < 256; i++) slut[i] = (int)(sinf(i * 6.2831853f / 256.0f) * 256.0f);

    // amplitudes in Q8 pixels (sub-pixel), keeping the strength scale at full precision
    int ampH = (int)(horizontal * strength * 256.0f);
    int ampV = (int)(vertical * strength * 256.0f);
    int phase = (int)((uint32_t)PV_TICKS / 12);   // animation drift
    const int freq = 6;                            // phase step per pixel (wavelength)

    int fx0, fy0, fx1, fy1;

    if(_pixel_format == RGB565) {
      if(!filter_rect(fx0, fy0, fx1, fy1)) return;
      // Row y reads rows y - reach .. y + reach + 1 (the +1 is the bilinear
      // tap), so a ring of 2 * reach + 2 rows holds every row still needed.
      int reach = ((ampV < 0 ? -ampV : ampV) + 256) >> 8;
      int n = 2 * reach + 2;
      if(n > H) n = H;
      image_t *ring = make_scratch_image(W, n);
      if(!ring) return;
      pv_fence_565();
      wave_ring_t rows = { ring, n };
      int next = fy0 - reach > 0 ? fy0 - reach : 0;   // next original row to unpack
      for(int y = fy0; y < fy1; y++) {
        int last = y + reach + 1 < H ? y + reach + 1 : H - 1;
        for(; next <= last; next++) {
          const uint16_t *s = (const uint16_t *)ptr(0, next);
          uint32_t *d = (uint32_t *)ring->ptr(0, next % n);
          for(int x = 0; x < W; x++) d[x] = pv_565_to_8888(s[x]);
        }
        int dxrow = ampH ? (ampH * slut[(y * freq + phase) & 0xff]) >> 8 : 0;   // Q8 px
        wave_row<pv_565_px>(rows, (uint16_t*)ptr(fx0, y), y, fx0, fx1, W, H, dxrow, ampV, phase, freq, slut, bilinear);
      }
      free_scratch_image(ring);
      return;
    }

    image_t *src = make_scratch_image(W, H);
    if(!src) return;
    for(int y = 0; y < H; y++)
      memcpy(src->ptr(0, y), ptr(0, y), (size_t)W * 4);

    if(!filter_rect(fx0, fy0, fx1, fy1)) { free_scratch_image(src); return; }

    wave_copy_t rows = { src };
    for(int y = fy0; y < fy1; y++) {
      int dxrow = ampH ? (ampH * slut[(y * freq + phase) & 0xff]) >> 8 : 0;   // Q8 px
      wave_row<pv_rgba_px>(rows, (uint32_t*)ptr(fx0, y), y, fx0, fx1, W, H, dxrow, ampV, phase, freq, slut, bilinear);
    }
    free_scratch_image(src);
  }

}
