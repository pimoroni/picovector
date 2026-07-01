#pragma once

#include <stdint.h>

#include "image.hpp"

namespace picovector {

  // --- source-fetch policies -------------------------------------------------
  // Each policy yields a *premultiplied* packed RGBA texel for column i of the
  // span. Templating the span cores over these lets the RGBA vs palette split be
  // a compile-time choice instead of a per-pixel branch, and lets the palette
  // path index a raw uint32_t* (no per-pixel out-of-line palette(i) call, no
  // by-value palette_t/std::vector copy per span).
  struct src_rgba {
    const uint32_t *base;
    inline __attribute__((always_inline)) uint32_t operator[](int i) const { return base[i]; }
  };
  struct src_pal {
    const uint8_t  *base;
    const uint32_t *pal;
    inline __attribute__((always_inline)) uint32_t operator[](int i) const { return pal[base[i]]; }
  };

  // --- templated span cores --------------------------------------------------
  // ApplyAlpha folds the image-level alpha into each source texel. When the
  // image is fully opaque (the common case) the caller picks ApplyAlpha=false
  // and the whole _premul_mul_alpha step compiles away — the invariant alpha
  // test is hoisted out of the per-pixel loop entirely. blend_over_premul is
  // always_inline, so there is no per-pixel indirect blend_func_t call.
  template<bool ApplyAlpha, typename Src>
  static inline __attribute__((always_inline))
  void span_over(Src src, uint32_t *pd, int w, uint32_t alpha) {
    for(int i = 0; i < w; i++) {
      uint32_t c = src[i];
      if(ApplyAlpha) c = _premul_mul_alpha(c, alpha);
      pd[i] = blend_over_premul(pd[i], c);
    }
  }

  template<bool ApplyAlpha, typename Src>
  static inline __attribute__((always_inline))
  void span_scale_over(Src src, uint32_t *pd, int w, fx16_t sx, fx16_t sx_step, int sw, uint32_t alpha) {
    for(int i = 0; i < w; i++) {
      int ix = sx >> 16;
      if(ix < 0) ix = 0; else if(ix >= sw) ix = sw - 1;
      uint32_t c = src[ix];
      if(ApplyAlpha) c = _premul_mul_alpha(c, alpha);
      pd[i] = blend_over_premul(pd[i], c);
      sx += sx_step;
    }
  }

  // --- public entry points ---------------------------------------------------
  // bf is retained for call-site/signature stability but is now unused: the only
  // blend mode is "over", inlined via blend_over_premul. Reintroduce a dispatch
  // here if additional blend modes are ever added.

  void span_blit(image_t *src, image_t *dst, blend_func_t bf, int sx, int sy, int dx, int dy, int w) {
    (void)bf;
    uint32_t *ps = (uint32_t *)src->ptr(sx, sy);
    uint32_t *pd = (uint32_t *)dst->ptr(dx, dy);
    uint32_t dst_alpha = dst->alpha();

    if(dst_alpha == 255u) span_over<false>(src_rgba{ps}, pd, w, 255u);
    else                  span_over<true >(src_rgba{ps}, pd, w, dst_alpha);
  }

  void span_blit(image_t *src, image_t *dst, blend_func_t bf, int sx, int sy, int dx, int dy, int w, const palette_t &palette) {
    (void)bf;
    uint8_t  *ps  = (uint8_t *)src->ptr(sx, sy);
    uint32_t *pd  = (uint32_t *)dst->ptr(dx, dy);
    const uint32_t *pal = palette.data();
    uint32_t dst_alpha = dst->alpha();

    if(dst_alpha == 255u) span_over<false>(src_pal{ps, pal}, pd, w, 255u);
    else                  span_over<true >(src_pal{ps, pal}, pd, w, dst_alpha);
  }

  void span_blit_scale(image_t *src, image_t *dst, blend_func_t bf, fx16_t sx, fx16_t sx_step, fx16_t sy, int dx, int dy, int w, filter_t filter = NEAREST) {
    (void)bf;
    uint32_t *pd = (uint32_t *)dst->ptr(dx, dy);
    uint32_t src_alpha = src->alpha();

    // NEAREST (and palette images, which sample() always resolves nearest): sy
    // is constant across the span, so the source row is fixed. Resolve it once
    // and index by the integer part of sx — this skips the per-pixel sample()
    // call along with its filter dispatch, bounds reload, iy clamp and y-axis
    // address maths. This is the common sprite/background blit path.
    if(filter == NEAREST || src->has_palette()) {
      rect_t b = src->bounds();
      int sw = (int)b.w;
      int sh = (int)b.h;
      int iy = sy >> 16;
      if(iy < 0) iy = 0; else if(iy >= sh) iy = sh - 1;

      if(src->has_palette()) {
        src_pal s{ (uint8_t *)src->ptr(0, iy), src->palette_data() };
        if(src_alpha == 255u) span_scale_over<false>(s, pd, w, sx, sx_step, sw, 255u);
        else                  span_scale_over<true >(s, pd, w, sx, sx_step, sw, src_alpha);
      } else {
        src_rgba s{ (uint32_t *)src->ptr(0, iy) };
        if(src_alpha == 255u) span_scale_over<false>(s, pd, w, sx, sx_step, sw, 255u);
        else                  span_scale_over<true >(s, pd, w, sx, sx_step, sw, src_alpha);
      }
      return;
    }

    // filtered (BILINEAR/BICUBIC) path: sample() does the interpolation, so we
    // can't fold the fetch into a raw row index.
    while(w--) {
      uint32_t c = src->sample(sx, sy, filter);
      if(src_alpha != 255u) c = _premul_mul_alpha(c, src_alpha);
      *pd = blend_over_premul(*pd, c);
      pd++;
      sx += sx_step;
    }
  }

  // palette images can't be interpolated, so sample() resolves them NEAREST;
  // the palette argument is no longer needed but kept for call-site compatibility
  void span_blit_scale(image_t *src, image_t *dst, blend_func_t bf, fx16_t sx, fx16_t sx_step, fx16_t sy, int dx, int dy, int w, const palette_t &palette, filter_t filter = NEAREST) {
    (void)palette;
    span_blit_scale(src, dst, bf, sx, sx_step, sy, dx, dy, w, filter);
  }

}
