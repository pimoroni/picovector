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

  // The halo is a blur, so at four bits a channel it bands like one: see blur().
  void image_t::bloom(int threshold, int intensity, float radius, float strength) {
    // An indexed image stores indices, not colours. See brush.cpp.
    if(_has_palette) return;
    intensity = (int)(intensity * strength);
    if(intensity <= 0) return;
    rect_t bd = bounds(); int W = (int)bd.w, H = (int)bd.h;
    if(W < 2 || H < 2) return;
    int hw = W / 2, hh = H / 2;
    image_t *tmp = make_scratch_image(hw, hh);
    if(!tmp) return;

    // bright pass at half res: one source texel per 2x2 block, thresholded
    for(int hy = 0; hy < hh; hy++) {
      pv_px srow = (pv_px)ptr(0, hy * 2);
      pv_px d = (pv_px)tmp->ptr(0, hy);
      for(int hx = 0; hx < hw; hx++) {
        pv_px s = srow + (hx * 2) * PV_PX_STEP;
        if(luminance(s) >= threshold) { pv_r(d) = pv_r(s); pv_g(d) = pv_g(s); pv_b(d) = pv_b(s); pv_a(d) = 255; }
        else { pv_word(d) = 0; }
        d += PV_PX_STEP;
      }
    }

    tmp->blur(radius * 0.5f);   // half res -> half the radius for the same spread

    // add the nearest-upsampled halo back over the full-res original. The bright
    // pass above reads the whole image on purpose - light outside the clip still
    // bleeds into it - but only the clip is written.
    int fx0, fy0, fx1, fy1;
    if(!filter_rect(fx0, fy0, fx1, fy1)) { free_scratch_image(tmp); return; }

    for(int y = fy0; y < fy1; y++) {
      int hy = y >> 1; if(hy >= hh) hy = hh - 1;
      pv_px s = (pv_px)ptr(fx0, y);
      pv_px hrow = (pv_px)tmp->ptr(0, hy);
      for(int x = fx0; x < fx1; x++) {
        int hx = x >> 1; if(hx >= hw) hx = hw - 1;
        pv_px h = hrow + hx * PV_PX_STEP;
        int r = pv_r(s) + ((pv_r(h) * intensity) >> 8); if(r > 255) r = 255;
        int g = pv_g(s) + ((pv_g(h) * intensity) >> 8); if(g > 255) g = 255;
        int b = pv_b(s) + ((pv_b(h) * intensity) >> 8); if(b > 255) b = 255;
        pv_r(s) = (uint8_t)r; pv_g(s) = (uint8_t)g; pv_b(s) = (uint8_t)b; // leave alpha
        s += PV_PX_STEP;
      }
    }
    free_scratch_image(tmp);
  }

}
