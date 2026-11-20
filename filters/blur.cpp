#include <cstdint>
#include <cmath>

#include "../picovector.hpp"
#include "../image.hpp"

#if PV_BLUR_INTERP
#include "hardware/interp.h"
#endif

#if PV_BLUR_DUAL_CORE
// The dual-core blur shares the rasteriser's single core1 worker rather than
// launching its own (two independent core1 launchers fight over the physical
// core and hard-fault). That worker only exists when PV_DUAL_CORE is on, so the
// blur split depends on it.
#if !PV_DUAL_CORE
#error "PV_BLUR_DUAL_CORE requires PV_DUAL_CORE (the shared core1 worker it dispatches to)"
#endif
extern "C" void pv_core1_run(void (*fn)());   // defined in picovector.cpp (rasteriser dispatcher)
extern "C" void pv_core1_join();
#endif

// The firmware builds -Os (size); the blur is a hot inner loop, so opt it -O3
// (helps the C++ fallback's per-channel IIR chains; harmless for the interp path).
#pragma GCC optimize("O3")

namespace picovector {

  static inline uint32_t blur_k_from_radius_q16(float radius) {
      if (radius <= 0) return 0;
      float a = std::exp(-1.0f / ((float)radius + 1.0f));   // a = exp(-1/(r+1)), k = 1-a
      float k = 1.0f - a;
      if (k < 0.0f) k = 0.0f;
      if (k > 0.9999f) k = 0.9999f;
      return (uint32_t)std::lround(k * 65536.0f);
  }

#if PV_BLUR_INTERP

  // Configure this core's INTERP0 for blend mode:
  //   peek[1] = base0 + ((base1-base0)*a)>>8,  a = ACCUM1[7:0]
  // i.e. the IIR lerp for one 8-bit channel, in hardware. The blend fraction is the
  // 8 LSBs of LANE1's shift+mask value, so the coefficient lives in ACCUM1 (not
  // ACCUM0 — LANE0's result is unused here). (A second unit didn't help — the
  // per-core SIO bus is the bottleneck, not the interpolator, so one suffices.)
  static inline void interp_iir_setup(uint32_t alpha8) {
    interp_config c0 = interp_default_config();
    interp_config_set_blend(&c0, true);
    interp_set_config(interp0, 0, &c0);
    interp_config c1 = interp_default_config();
    interp_config_set_signed(&c1, true);            // signed blend: base1-base0 can be < 0
    interp_set_config(interp0, 1, &c1);
    interp0->accum[1] = alpha8;
  }

  // Channels are handled independently, so load/store them as bytes directly — no
  // pack/unpack. Seed the state from the pixel at p.
  static inline void interp_seed(const uint8_t *p, uint32_t &yr, uint32_t &yg,
                                 uint32_t &yb, uint32_t &ya) {
    yr = p[0]; yg = p[1]; yb = p[2]; ya = p[3];
  }

  // one IIR step: blend each channel toward its state via INTERP0. Bytes in place.
  static inline void interp_step(uint8_t *p, uint32_t &yr, uint32_t &yg,
                                 uint32_t &yb, uint32_t &ya) {
    interp0->base[0] = yr; interp0->base[1] = p[0]; yr = interp0->peek[1];
    interp0->base[0] = yg; interp0->base[1] = p[1]; yg = interp0->peek[1];
    interp0->base[0] = yb; interp0->base[1] = p[2]; yb = interp0->peek[1];
    interp0->base[0] = ya; interp0->base[1] = p[3]; ya = interp0->peek[1];
    p[0] = yr; p[1] = yg; p[2] = yb; p[3] = ya;
  }

  static void blur_hpass(uint8_t *buffer, size_t stride, uint32_t k,
                         int x0, int x1, int y0, int y1) {
    interp_iir_setup(k >> 8);
    for (int y = y0; y < y1; ++y) {
      uint8_t *row = buffer + (size_t)y * stride;
      uint32_t yr, yg, yb, ya;
      interp_seed(row + x0 * 4, yr, yg, yb, ya);
      for (int x = x0 + 1; x < x1; ++x) interp_step(row + x * 4, yr, yg, yb, ya);
      interp_seed(row + (x1 - 1) * 4, yr, yg, yb, ya);
      for (int x = x1 - 2; x >= x0; --x) interp_step(row + x * 4, yr, yg, yb, ya);
    }
  }

  static void blur_vpass(uint8_t *buffer, size_t stride, uint32_t k,
                         int x0, int x1, int y0, int y1) {
    interp_iir_setup(k >> 8);
    for (int x = x0; x < x1; ++x) {
      uint8_t *col = buffer + x * 4;
      uint32_t yr, yg, yb, ya;
      interp_seed(col + (size_t)y0 * stride, yr, yg, yb, ya);
      for (int y = y0 + 1; y < y1; ++y) interp_step(col + (size_t)y * stride, yr, yg, yb, ya);
      interp_seed(col + (size_t)(y1 - 1) * stride, yr, yg, yb, ya);
      for (int y = y1 - 2; y >= y0; --y) interp_step(col + (size_t)y * stride, yr, yg, yb, ya);
    }
  }

#else   // ---- portable C++ fallback (blocked vertical for ILP) ----

  static inline int iir_step_q16(int y, int x, uint32_t k) {
    int d = x - y; return y + (int)((d * (int32_t)k) >> 16);
  }
  static void blur_hpass(uint8_t *buffer, size_t stride, uint32_t k,
                         int x0, int x1, int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
      uint8_t *row = buffer + (size_t)y * stride, *p = row + x0 * 4;
      int r = p[0], g = p[1], b = p[2], a = p[3];
      for (int x = x0 + 1; x < x1; ++x) { p = row + x * 4;
        r = iir_step_q16(r, p[0], k); g = iir_step_q16(g, p[1], k);
        b = iir_step_q16(b, p[2], k); a = iir_step_q16(a, p[3], k);
        p[0] = r; p[1] = g; p[2] = b; p[3] = a; }
      p = row + (x1 - 1) * 4; r = p[0]; g = p[1]; b = p[2]; a = p[3];
      for (int x = x1 - 2; x >= x0; --x) { p = row + x * 4;
        r = iir_step_q16(r, p[0], k); g = iir_step_q16(g, p[1], k);
        b = iir_step_q16(b, p[2], k); a = iir_step_q16(a, p[3], k);
        p[0] = r; p[1] = g; p[2] = b; p[3] = a; }
    }
  }
  static void blur_vpass(uint8_t *buffer, size_t stride, uint32_t k,
                         int x0, int x1, int y0, int y1) {
    const int B = 16; int rr[B], gg[B], bb[B], aa[B];
    for (int xb = x0; xb < x1; xb += B) {
      int bw = x1 - xb; if (bw > B) bw = B;
      for (int c = 0; c < bw; ++c) { uint8_t *p = buffer + (size_t)y0 * stride + (xb + c) * 4;
        rr[c] = p[0]; gg[c] = p[1]; bb[c] = p[2]; aa[c] = p[3]; }
      for (int y = y0 + 1; y < y1; ++y) { uint8_t *rp = buffer + (size_t)y * stride + xb * 4;
        for (int c = 0; c < bw; ++c) { uint8_t *p = rp + c * 4;
          rr[c] = iir_step_q16(rr[c], p[0], k); gg[c] = iir_step_q16(gg[c], p[1], k);
          bb[c] = iir_step_q16(bb[c], p[2], k); aa[c] = iir_step_q16(aa[c], p[3], k);
          p[0] = rr[c]; p[1] = gg[c]; p[2] = bb[c]; p[3] = aa[c]; } }
      for (int c = 0; c < bw; ++c) { uint8_t *p = buffer + (size_t)(y1 - 1) * stride + (xb + c) * 4;
        rr[c] = p[0]; gg[c] = p[1]; bb[c] = p[2]; aa[c] = p[3]; }
      for (int y = y1 - 2; y >= y0; --y) { uint8_t *rp = buffer + (size_t)y * stride + xb * 4;
        for (int c = 0; c < bw; ++c) { uint8_t *p = rp + c * 4;
          rr[c] = iir_step_q16(rr[c], p[0], k); gg[c] = iir_step_q16(gg[c], p[1], k);
          bb[c] = iir_step_q16(bb[c], p[2], k); aa[c] = iir_step_q16(aa[c], p[3], k);
          p[0] = rr[c]; p[1] = gg[c]; p[2] = bb[c]; p[3] = aa[c]; } }
    }
  }

#endif

  // ---- RGB565 ----
  // The same two passes, run in place on the 16-bit pixels. Each pass writes
  // 8-bit channels that the next step reads back, so the bits the 565 pack
  // drops (3 red, 2 green, 3 blue) are kept in a byte a pixel beside them:
  // every step then sees exactly the value the RGBA blur would, and the result
  // is the packed RGBA blur. Alpha is always opaque on a 565 image, so only
  // the three colour channels are stepped.

#if PV_BLUR_INTERP
  static inline void iir565_setup(uint32_t k) { interp_iir_setup(k >> 8); }
  static inline uint32_t iir565_step(uint32_t y, uint32_t x, uint32_t k) {
    interp0->base[0] = y; interp0->base[1] = x; return interp0->peek[1];
  }
#else
  static inline void iir565_setup(uint32_t k) { (void)k; }
  static inline uint32_t iir565_step(uint32_t y, uint32_t x, uint32_t k) {
    return (uint32_t)iir_step_q16((int)y, (int)x, k);
  }
#endif

  // the 565 pixel plus its dropped low bits -> 8-bit channels, and back
  static inline void res565_get(uint16_t p, uint8_t q, uint32_t &r, uint32_t &g, uint32_t &b) {
    r = ((p >> 8) & 0xf8u) | (q & 0x07u);
    g = ((p >> 3) & 0xfcu) | ((q >> 3) & 0x03u);
    b = ((p << 3) & 0xf8u) | (q >> 5);
  }
  static inline void res565_put(uint16_t &p, uint8_t &q, uint32_t r, uint32_t g, uint32_t b) {
    p = (uint16_t)(((r & 0xf8u) << 8) | ((g & 0xfcu) << 3) | (b >> 3));
    q = (uint8_t)((r & 0x07u) | ((g & 0x03u) << 3) | ((b & 0x07u) << 5));
  }

  // The image's pixels and the low-bit map, which covers only the clip
  // (origin rx, ry).
  struct blur565_t {
    uint16_t *fb; size_t stride;
    uint8_t *res; size_t res_stride; int rx, ry;
    uint32_t k;
    inline uint16_t *px(int x, int y) const { return fb + (size_t)y * stride + x; }
    inline uint8_t *lo(int x, int y) const { return res + (size_t)(y - ry) * res_stride + (x - rx); }
  };

  // Seeded from the first pixel as it unpacks, like the RGBA pass, then the
  // backward sweep starts from the forward state, which is the last pixel.
  static void blur565_hpass(const blur565_t &j, int x0, int x1, int y0, int y1) {
    iir565_setup(j.k);
    for (int y = y0; y < y1; ++y) {
      uint16_t *p = j.px(0, y);
      uint8_t *q = j.lo(0, y);
      uint32_t c = pv_565_to_8888(p[x0]);
      uint32_t r = c & 0xff, g = (c >> 8) & 0xff, b = (c >> 16) & 0xff;
      res565_put(p[x0], q[x0], r, g, b);
      for (int x = x0 + 1; x < x1; ++x) {
        c = pv_565_to_8888(p[x]);
        r = iir565_step(r, c & 0xff, j.k);
        g = iir565_step(g, (c >> 8) & 0xff, j.k);
        b = iir565_step(b, (c >> 16) & 0xff, j.k);
        res565_put(p[x], q[x], r, g, b);
      }
      for (int x = x1 - 2; x >= x0; --x) {
        uint32_t cr, cg, cb;
        res565_get(p[x], q[x], cr, cg, cb);
        r = iir565_step(r, cr, j.k); g = iir565_step(g, cg, j.k); b = iir565_step(b, cb, j.k);
        res565_put(p[x], q[x], r, g, b);
      }
    }
  }

  // Walked a row at a time rather than a column: each column's state is the
  // row before it (forward) or after it (backward), already stored at full
  // precision, so no state array is needed and rows stay sequential in memory.
  static void blur565_vpass(const blur565_t &j, int x0, int x1, int y0, int y1) {
    iir565_setup(j.k);
    for (int y = y0 + 1; y < y1; ++y) {
      uint16_t *sp = j.px(0, y - 1), *p = j.px(0, y);
      uint8_t *sq = j.lo(0, y - 1), *q = j.lo(0, y);
      for (int x = x0; x < x1; ++x) {
        uint32_t r, g, b, cr, cg, cb;
        res565_get(sp[x], sq[x], r, g, b);
        res565_get(p[x], q[x], cr, cg, cb);
        res565_put(p[x], q[x], iir565_step(r, cr, j.k), iir565_step(g, cg, j.k), iir565_step(b, cb, j.k));
      }
    }
    for (int y = y1 - 2; y >= y0; --y) {
      uint16_t *sp = j.px(0, y + 1), *p = j.px(0, y);
      uint8_t *sq = j.lo(0, y + 1), *q = j.lo(0, y);
      for (int x = x0; x < x1; ++x) {
        uint32_t r, g, b, cr, cg, cb;
        res565_get(sp[x], sq[x], r, g, b);
        res565_get(p[x], q[x], cr, cg, cb);
        res565_put(p[x], q[x], iir565_step(r, cr, j.k), iir565_step(g, cg, j.k), iir565_step(b, cb, j.k));
      }
    }
  }

#if PV_BLUR_DUAL_CORE
  namespace {
    struct blur_band_t { uint8_t *buf; size_t stride; uint32_t k; int x0, x1, y0, y1; bool vertical; const blur565_t *fb565; };
    blur_band_t g_blur_band;
    void blur_core1_band() {
      blur_band_t j = g_blur_band;
      if (j.fb565) {
        if (j.vertical) blur565_vpass(*j.fb565, j.x0, j.x1, j.y0, j.y1);
        else            blur565_hpass(*j.fb565, j.x0, j.x1, j.y0, j.y1);
      }
      else if (j.vertical) blur_vpass(j.buf, j.stride, j.k, j.x0, j.x1, j.y0, j.y1);
      else                 blur_hpass(j.buf, j.stride, j.k, j.x0, j.x1, j.y0, j.y1);
    }
  }
  static const int PV_BLUR_MIN_SPLIT = 64;
#endif

  // Both passes over the clip on a 565 image, split across the cores the same
  // way as the RGBA blur. The low-bit map is the clip's width * height bytes
  // (75KB for 320x240): the working buffer when it fits, the heap otherwise.
  static void blur565(image_t *img, uint32_t k, int x0, int x1, int y0, int y1) {
    size_t rw = (size_t)(x1 - x0), bytes = rw * (size_t)(y1 - y0);
    bool borrowed = bytes <= working_buffer_size;
    uint8_t *res = borrowed ? (uint8_t *)PicoVector_working_buffer : (uint8_t *)PV_MALLOC_NO_SCAN(bytes);
    if (!res) return;
    const blur565_t j = { (uint16_t *)img->ptr(0, 0), img->row_stride() / sizeof(uint16_t), res, rw, x0, y0, k };

#if PV_BLUR_DUAL_CORE
    if (y1 - y0 >= PV_BLUR_MIN_SPLIT) {
      int ymid = y0 + (y1 - y0) / 2;
      g_blur_band = { nullptr, 0, k, x0, x1, ymid, y1, false, &j };
      pv_core1_run(blur_core1_band);
      blur565_hpass(j, x0, x1, y0, ymid);
      pv_core1_join();
    } else {
      blur565_hpass(j, x0, x1, y0, y1);
    }
    if (x1 - x0 >= PV_BLUR_MIN_SPLIT) {
      int xmid = x0 + (x1 - x0) / 2;
      g_blur_band = { nullptr, 0, k, xmid, x1, y0, y1, true, &j };
      pv_core1_run(blur_core1_band);
      blur565_vpass(j, x0, xmid, y0, y1);
      pv_core1_join();
    } else {
      blur565_vpass(j, x0, x1, y0, y1);
    }
#else
    blur565_hpass(j, x0, x1, y0, y1);
    blur565_vpass(j, x0, x1, y0, y1);
#endif

    if (!borrowed) {
#if MICROPY_MALLOC_USES_ALLOCATED_SIZE
      PV_FREE(res, bytes);
#else
      PV_FREE(res);
#endif
    }
  }

  void image_t::blur(float radius, float strength) {
    // An indexed image is one byte a pixel; this writes four. See brush.cpp.
    if(_has_palette) return;
    radius *= strength;
    if (radius <= 0) return;
    const uint32_t k = blur_k_from_radius_q16(radius);
    if (k == 0) return;

    const int width = int(_bounds.w), height = int(_bounds.h);
    rect_t cr = _clip.intersection(_bounds);
    if (cr.empty()) return;
    int x0 = int(cr.x);          if (x0 < 0) x0 = 0;
    int y0 = int(cr.y);          if (y0 < 0) y0 = 0;
    int x1 = int(cr.x + cr.w);   if (x1 > width) x1 = width;
    int y1 = int(cr.y + cr.h);   if (y1 > height) y1 = height;
    if (x1 - x0 < 1 || y1 - y0 < 1) return;

    if (_pixel_format == RGB565) {
      pv_fence_565();
      blur565(this, k, x0, x1, y0, y1);
      return;
    }

    uint8_t *buf = (uint8_t *)_buffer;
    size_t stride = _row_stride;

#if PV_BLUR_DUAL_CORE
    if (y1 - y0 >= PV_BLUR_MIN_SPLIT) {                   // H pass — split rows across cores
      int ymid = y0 + (y1 - y0) / 2;
      g_blur_band = { buf, stride, k, x0, x1, ymid, y1, false };
      pv_core1_run(blur_core1_band);
      blur_hpass(buf, stride, k, x0, x1, y0, ymid);
      pv_core1_join();
    } else {
      blur_hpass(buf, stride, k, x0, x1, y0, y1);         // too small to be worth splitting
    }
#else
    blur_hpass(buf, stride, k, x0, x1, y0, y1);
#endif

#if PV_BLUR_DUAL_CORE
    if (x1 - x0 >= PV_BLUR_MIN_SPLIT) {                   // V pass — split columns across cores
      int xmid = x0 + (x1 - x0) / 2;
      g_blur_band = { buf, stride, k, xmid, x1, y0, y1, true };
      pv_core1_run(blur_core1_band);
      blur_vpass(buf, stride, k, x0, xmid, y0, y1);
      pv_core1_join();
    } else {
      blur_vpass(buf, stride, k, x0, x1, y0, y1);         // too small to be worth splitting
    }
#else
    blur_vpass(buf, stride, k, x0, x1, y0, y1);
#endif
  }

}
