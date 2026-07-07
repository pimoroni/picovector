#include <cstdint>
#include <cmath>

#include "../picovector.hpp"
#include "../image.hpp"

// On the device, do the per-channel IIR lerp with an RP2350 hardware interpolator
// (blend mode). We also benchmarked a hand-tuned M33 SIMD version; it was no faster
// (~21ms/frame either way — the blur is limited by per-pixel bus/compute throughput,
// not the arithmetic), so the interpolator wins on readability. Host builds have no
// interpolator, so fall back to portable C++.
#if defined(__has_include)
#  if __has_include("hardware/interp.h")
#    include "hardware/interp.h"
#    define PV_BLUR_INTERP 1
#  endif
#endif

// core1 is available (and owned by picovector) on the device build; use it to split
// each pass across both cores. Host builds fall back to single-core.
#if defined(__has_include)
#  if __has_include("pico/multicore.h")
#    define PV_BLUR_DUAL_CORE 1
#  endif
#endif
#if PV_BLUR_DUAL_CORE
extern "C" void pv_core1_run(void (*fn)());   // defined in picovector.cpp
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
  //   peek[1] = base0 + ((base1-base0)*a)>>8,  a = ACCUM0[7:0]
  // i.e. the IIR lerp for one 8-bit channel, in hardware. (A second unit didn't help
  // — the per-core SIO bus is the bottleneck, not the interpolator, so one suffices.)
  static inline void interp_iir_setup(uint32_t alpha8) {
    interp_config c0 = interp_default_config();
    interp_config_set_blend(&c0, true);
    interp_set_config(interp0, 0, &c0);
    interp_config c1 = interp_default_config();
    interp_config_set_signed(&c1, true);            // signed blend: base1-base0 can be < 0
    interp_set_config(interp0, 1, &c1);
    interp0->accum[0] = alpha8;
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

#if PV_BLUR_DUAL_CORE
  namespace {
    struct blur_band_t { uint8_t *buf; size_t stride; uint32_t k; int x0, x1, y0, y1; bool vertical; };
    blur_band_t g_blur_band;
    void blur_core1_band() {
      blur_band_t j = g_blur_band;
      if (j.vertical) blur_vpass(j.buf, j.stride, j.k, j.x0, j.x1, j.y0, j.y1);
      else            blur_hpass(j.buf, j.stride, j.k, j.x0, j.x1, j.y0, j.y1);
    }
  }
  static const int PV_BLUR_MIN_SPLIT = 64;
#endif

  void image_t::blur(float radius) {
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
