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

  // Read and write the image through PX, so a 565 image takes the same
  // arithmetic as an RGBA one. See pv_565_px in image.hpp.
  template <typename PX>
  static void bloom_bright_pass(image_t *img, image_t *tmp, int threshold, int hw, int hh) {
    for(int hy = 0; hy < hh; hy++) {
      const typename PX::raw *srow = (const typename PX::raw *)img->ptr(0, hy * 2);
      uint8_t *d = (uint8_t*)tmp->ptr(0, hy);
      for(int hx = 0; hx < hw; hx++) {
        uint32_t v = PX::unpack(srow[hx * 2]);
        uint8_t *s = (uint8_t*)&v;
        if(luminance(s) >= threshold) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255; }
        else { *(uint32_t*)d = 0; }
        d += 4;
      }
    }
  }

  template <typename PX>
  static void bloom_add(image_t *img, image_t *tmp, int intensity, int hw, int hh, int fx0, int fy0, int fx1, int fy1) {
    for(int y = fy0; y < fy1; y++) {
      int hy = y >> 1; if(hy >= hh) hy = hh - 1;
      typename PX::raw *row = (typename PX::raw *)img->ptr(fx0, y);
      uint8_t *hrow = (uint8_t*)tmp->ptr(0, hy);
      for(int x = fx0; x < fx1; x++) {
        int hx = x >> 1; if(hx >= hw) hx = hw - 1;
        uint8_t *h = hrow + hx * 4;
        uint32_t v = PX::unpack(*row);
        uint8_t *s = (uint8_t*)&v;
        int r = s[0] + ((h[0] * intensity) >> 8); if(r > 255) r = 255;
        int g = s[1] + ((h[1] * intensity) >> 8); if(g > 255) g = 255;
        int b = s[2] + ((h[2] * intensity) >> 8); if(b > 255) b = 255;
        s[0] = (uint8_t)r; s[1] = (uint8_t)g; s[2] = (uint8_t)b; // leave alpha
        *row++ = PX::pack(v);
      }
    }
  }

  void image_t::bloom(int threshold, int intensity, float radius, float strength) {
    // An indexed image is one byte a pixel; this writes four. See brush.cpp.
    if(_has_palette) return;
    intensity = (int)(intensity * strength);
    if(intensity <= 0) return;
    rect_t bd = bounds(); int W = (int)bd.w, H = (int)bd.h;
    if(W < 2 || H < 2) return;
    int hw = W / 2, hh = H / 2;
    image_t *tmp = make_scratch_image(hw, hh);
    if(!tmp) return;
    bool is565 = _pixel_format == RGB565;

    // bright pass at half res: one source texel per 2x2 block, thresholded
    if(is565) bloom_bright_pass<pv_565_px>(this, tmp, threshold, hw, hh);
    else      bloom_bright_pass<pv_rgba_px>(this, tmp, threshold, hw, hh);

    tmp->blur(radius * 0.5f);   // half res -> half the radius for the same spread

    // add the nearest-upsampled halo back over the full-res original. The bright
    // pass above reads the whole image on purpose - light outside the clip still
    // bleeds into it - but only the clip is written.
    int fx0, fy0, fx1, fy1;
    if(!filter_rect(fx0, fy0, fx1, fy1)) { free_scratch_image(tmp); return; }

    if(is565) {
      pv_fence_565();
      bloom_add<pv_565_px>(this, tmp, intensity, hw, hh, fx0, fy0, fx1, fy1);
    } else {
      bloom_add<pv_rgba_px>(this, tmp, intensity, hw, hh, fx0, fy0, fx1, fy1);
    }
    free_scratch_image(tmp);
  }

}
