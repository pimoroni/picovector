#pragma once

#include <stdint.h>

#include "image.hpp"

namespace picovector {

  // --- source-fetch policies -------------------------------------------------
  // Each policy yields a *premultiplied* packed RGBA texel for column i of the
  // span. Templating the span cores over these lets the RGBA vs palette split be
  // a compile-time choice instead of a per-pixel branch, and lets the palette
  // path index a raw uint32_t* (no per-pixel out-of-line palette(i) call, no
  // by-value palette copy per span).
  // stored(i) is the same texel in the framebuffer's stored form, for the span
  // cores that composite without touching it first.
  struct src_rgba {
    const pv_store_t *base;
    inline __attribute__((always_inline)) uint32_t operator[](int i) const { return pv_load(&base[i]); }
    inline __attribute__((always_inline)) pv_store_t stored(int i) const { return base[i]; }
  };
  struct src_pal {
    const uint8_t  *base;
    const uint32_t *pal;
    inline __attribute__((always_inline)) uint32_t operator[](int i) const { return pal[base[i]]; }
    inline __attribute__((always_inline)) pv_store_t stored(int i) const { return pv_pack(pal[base[i]]); }
  };

  // --- templated span cores --------------------------------------------------
  // ApplyAlpha folds the target's global alpha into each source texel. When the
  // target is fully opaque (the common case) the caller picks ApplyAlpha=false
  // and the whole _premul_mul_alpha step compiles away — the invariant alpha
  // test is hoisted out of the per-pixel loop entirely. blend_over_premul is
  // always_inline, so there is no per-pixel indirect blend_func_t call.
  template<bool ApplyAlpha, typename Src>
  static inline __attribute__((always_inline))
  void span_over(Src src, pv_store_t *pd, int w, uint32_t alpha) {
    for(int i = 0; i < w; i++) {
      if(ApplyAlpha) {
        uint32_t c = src[i];
        c = _premul_mul_alpha(c, alpha);
        pv_blend_over(&pd[i], c);
      } else {
        pv_store_t s = src.stored(i);
        pv_blend_stored(&pd[i], s);
      }
    }
  }

  template<bool ApplyAlpha, typename Src>
  static inline __attribute__((always_inline))
  void span_scale_over(Src src, pv_store_t *pd, int w, fx16_t sx, fx16_t sx_step, int sw, uint32_t alpha) {
    // ix = sx>>16 is monotonic in i (the step is fixed), so if both span
    // endpoints land in [0, sw) then every pixel does. The caller clips the
    // source rect to the source bounds, so that is the common case: when it holds
    // we drop the per-pixel bounds test entirely. Edge-crossing spans (sub-texel
    // rounding at the last pixel, or a source rect spilling past an edge) fall
    // back to the clamped loop, which is byte-identical to the always-clamped
    // path. The endpoint is evaluated once per span in Q16, with a 64-bit
    // intermediate so w*step can't overflow (w and sw carry the fixed-point
    // extent, not just a pixel count).
    int64_t ex = (int64_t)sx + (int64_t)(w - 1) * sx_step; // Q16 end coordinate
    if((unsigned)(sx >> 16) < (unsigned)sw && ex >= 0 && (ex >> 16) < sw) {
      for(int i = 0; i < w; i++) {
        if(ApplyAlpha) {
          uint32_t c = src[sx >> 16];
          c = _premul_mul_alpha(c, alpha);
          pv_blend_over(&pd[i], c);
        } else {
          pv_store_t s = src.stored(sx >> 16);
          pv_blend_stored(&pd[i], s);
        }
        sx += sx_step;
      }
    } else {
      for(int i = 0; i < w; i++) {
        int ix = sx >> 16;
        if(ix < 0) ix = 0; else if(ix >= sw) ix = sw - 1;
        if(ApplyAlpha) {
          uint32_t c = src[ix];
          c = _premul_mul_alpha(c, alpha);
          pv_blend_over(&pd[i], c);
        } else {
          pv_store_t s = src.stored(ix);
          pv_blend_stored(&pd[i], s);
        }
        sx += sx_step;
      }
    }
  }

  // --- public entry points ---------------------------------------------------
  // bf is retained for call-site/signature stability but is now unused: the only
  // blend mode is "over", inlined via blend_over_premul. Reintroduce a dispatch
  // here if additional blend modes are ever added.

  inline void span_blit(image_t *src, image_t *dst, blend_func_t bf, int sx, int sy, int dx, int dy, int w) {
    (void)bf;
    pv_store_t *ps = (pv_store_t *)src->ptr(sx, sy);
    pv_store_t *pd = (pv_store_t *)dst->ptr(dx, dy);
    uint32_t dst_alpha = dst->alpha();

    if(dst_alpha == 255u) span_over<false>(src_rgba{ps}, pd, w, 255u);
    else                  span_over<true >(src_rgba{ps}, pd, w, dst_alpha);
  }

  inline void span_blit(image_t *src, image_t *dst, blend_func_t bf, int sx, int sy, int dx, int dy, int w, const uint32_t *palette) {
    (void)bf;
    uint8_t  *ps  = (uint8_t *)src->ptr(sx, sy);
    pv_store_t *pd  = (pv_store_t *)dst->ptr(dx, dy);
    const uint32_t *pal = palette;
    uint32_t dst_alpha = dst->alpha();

    if(dst_alpha == 255u) span_over<false>(src_pal{ps, pal}, pd, w, 255u);
    else                  span_over<true >(src_pal{ps, pal}, pd, w, dst_alpha);
  }

  inline void span_blit_scale(image_t *src, image_t *dst, blend_func_t bf, fx16_t sx, fx16_t sx_step, fx16_t sy, int dx, int dy, int w, filter_t filter = NEAREST) {
    (void)bf;
    pv_store_t *pd = (pv_store_t *)dst->ptr(dx, dy);
    uint32_t dst_alpha = dst->alpha();

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
        if(dst_alpha == 255u) span_scale_over<false>(s, pd, w, sx, sx_step, sw, 255u);
        else                  span_scale_over<true >(s, pd, w, sx, sx_step, sw, dst_alpha);
      } else {
        src_rgba s{ (pv_store_t *)src->ptr(0, iy) };
        if(dst_alpha == 255u) span_scale_over<false>(s, pd, w, sx, sx_step, sw, 255u);
        else                  span_scale_over<true >(s, pd, w, sx, sx_step, sw, dst_alpha);
      }
      return;
    }

    // filtered (BILINEAR/BICUBIC) path: sample() does the interpolation, so we
    // can't fold the fetch into a raw row index.
    while(w--) {
      uint32_t c = src->sample(sx, sy, filter);
      if(dst_alpha != 255u) c = _premul_mul_alpha(c, dst_alpha);
      pv_blend_over(pd, c);
      pd++;
      sx += sx_step;
    }
  }

  // palette images can't be interpolated, so sample() resolves them NEAREST;
  // the palette argument is no longer needed but kept for call-site compatibility
  inline void span_blit_scale(image_t *src, image_t *dst, blend_func_t bf, fx16_t sx, fx16_t sx_step, fx16_t sy, int dx, int dy, int w, const uint32_t *palette, filter_t filter = NEAREST) {
    (void)palette;
    span_blit_scale(src, dst, bf, sx, sx_step, sy, dx, dy, w, filter);
  }

}
