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
  struct src_rgba {
    const uint32_t *base;
    inline __attribute__((always_inline)) uint32_t operator[](int i) const { return base[i]; }
  };
  struct src_pal {
    const uint8_t  *base;
    const uint32_t *pal;
    inline __attribute__((always_inline)) uint32_t operator[](int i) const { return pal[base[i]]; }
  };
  // A 16-bit (RGB565) source - the platform framebuffer. Its pixels are opaque,
  // so the unpacked texel is already premultiplied.
  struct src_565 {
    const uint16_t *base;
    inline __attribute__((always_inline)) uint32_t operator[](int i) const { return pv_565_to_8888(base[i]); }
  };

  // --- destination policies ----------------------------------------------------
  // The span cores below read-blend-write through one of these, so an RGB565
  // destination (the platform framebuffer) costs an unpack and a pack per pixel
  // instead of a whole staged row, and the RGBA path compiles to what it always
  // was.
  struct dst_rgba {
    uint32_t *base;
    inline __attribute__((always_inline)) uint32_t get(int i) const { return base[i]; }
    inline __attribute__((always_inline)) void put(int i, uint32_t c) const { base[i] = c; }
  };
  struct dst_565 {
    uint16_t *base;
    inline __attribute__((always_inline)) uint32_t get(int i) const { return pv_565_to_8888(base[i]); }
    inline __attribute__((always_inline)) void put(int i, uint32_t c) const { base[i] = pv_8888_to_565(c); }
  };

  // --- templated span cores --------------------------------------------------
  // ApplyAlpha folds the target's global alpha into each source texel. When the
  // target is fully opaque (the common case) the caller picks ApplyAlpha=false
  // and the whole _premul_mul_alpha step compiles away — the invariant alpha
  // test is hoisted out of the per-pixel loop entirely. blend_over_premul is
  // always_inline, so there is no per-pixel indirect blend_func_t call.
  template<bool ApplyAlpha, typename Src, typename Dst>
  static inline __attribute__((always_inline))
  void span_over(Src src, Dst pd, int w, uint32_t alpha) {
    for(int i = 0; i < w; i++) {
      uint32_t c = src[i];
      if(ApplyAlpha) c = _premul_mul_alpha(c, alpha);
      pd.put(i, blend_over_premul(pd.get(i), c));
    }
  }

  template<bool ApplyAlpha, typename Src, typename Dst>
  static inline __attribute__((always_inline))
  void span_scale_over(Src src, Dst pd, int w, fx16_t sx, fx16_t sx_step, int sw, uint32_t alpha) {
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
        uint32_t c = src[sx >> 16];
        if(ApplyAlpha) c = _premul_mul_alpha(c, alpha);
        pd.put(i, blend_over_premul(pd.get(i), c));
        sx += sx_step;
      }
    } else {
      for(int i = 0; i < w; i++) {
        int ix = sx >> 16;
        if(ix < 0) ix = 0; else if(ix >= sw) ix = sw - 1;
        uint32_t c = src[ix];
        if(ApplyAlpha) c = _premul_mul_alpha(c, alpha);
        pd.put(i, blend_over_premul(pd.get(i), c));
        sx += sx_step;
      }
    }
  }

  // BILINEAR along one span. sy is constant across the span, so the two source
  // rows and the vertical fraction are resolved once by the caller. As in
  // span_scale_over, when both span endpoints keep x and x + 1 inside the source
  // no texel needs clamping; otherwise the clamped loop matches sample()'s edge
  // handling.
  template<bool ApplyAlpha, typename Src, typename Dst>
  static inline __attribute__((always_inline))
  void span_bilinear_over(Src r0, Src r1, Dst pd, int w, fx16_t sx, fx16_t sx_step, int sw, uint32_t fy, uint32_t alpha) {
    int64_t ex = (int64_t)sx + (int64_t)(w - 1) * sx_step; // Q16 end coordinate
    int xa = sx >> 16;
    int64_t xb = ex >> 16;
    if(xa >= 0 && xb >= 0 && xa + 1 < sw && xb + 1 < sw) {
      for(int i = 0; i < w; i++) {
        int x = sx >> 16;
        uint32_t fx = (sx >> 8) & 0xffu;
        uint32_t c = _lerp_premul(_lerp_premul(r0[x], r0[x + 1], fx), _lerp_premul(r1[x], r1[x + 1], fx), fy);
        if(ApplyAlpha) c = _premul_mul_alpha(c, alpha);
        pd.put(i, blend_over_premul(pd.get(i), c));
        sx += sx_step;
      }
    } else {
      for(int i = 0; i < w; i++) {
        int ix = sx >> 16;
        uint32_t fx = (sx >> 8) & 0xffu;
        int x0 = ix, x1 = ix + 1;
        if(x0 < 0) x0 = 0; else if(x0 >= sw) x0 = sw - 1;
        if(x1 < 0) x1 = 0; else if(x1 >= sw) x1 = sw - 1;
        uint32_t c = _lerp_premul(_lerp_premul(r0[x0], r0[x1], fx), _lerp_premul(r1[x0], r1[x1], fx), fy);
        if(ApplyAlpha) c = _premul_mul_alpha(c, alpha);
        pd.put(i, blend_over_premul(pd.get(i), c));
        sx += sx_step;
      }
    }
  }

  // --- public entry points ---------------------------------------------------
  // bf is retained for call-site/signature stability but is now unused: the only
  // blend mode is "over", inlined via blend_over_premul. Reintroduce a dispatch
  // here if additional blend modes are ever added.

  // One row of a plain blit, dispatched over the source and destination pixel
  // policies. The generic core takes any src/dst pairing; the RGBA/RGBA case
  // compiles to exactly the loop this always was.
  template<typename Src>
  inline void _span_blit_to(Src s, image_t *dst, int dx, int dy, int w) {
    uint32_t dst_alpha = dst->alpha();
    if(dst->pixel_format() == RGB565) {
      pv_fence_565();                                // scan-out may still be reading
      dst_565 d{ (uint16_t *)dst->ptr(dx, dy) };
      if(dst_alpha == 255u) span_over<false>(s, d, w, 255u);
      else                  span_over<true >(s, d, w, dst_alpha);
    } else {
      dst_rgba d{ (uint32_t *)dst->ptr(dx, dy) };
      if(dst_alpha == 255u) span_over<false>(s, d, w, 255u);
      else                  span_over<true >(s, d, w, dst_alpha);
    }
  }

  inline void span_blit(image_t *src, image_t *dst, blend_func_t bf, int sx, int sy, int dx, int dy, int w) {
    (void)bf;
    if(src->pixel_format() == RGB565)
      _span_blit_to(src_565{ (uint16_t *)src->ptr(sx, sy) }, dst, dx, dy, w);
    else
      _span_blit_to(src_rgba{ (uint32_t *)src->ptr(sx, sy) }, dst, dx, dy, w);
  }

  inline void span_blit(image_t *src, image_t *dst, blend_func_t bf, int sx, int sy, int dx, int dy, int w, const uint32_t *palette) {
    (void)bf;
    _span_blit_to(src_pal{ (uint8_t *)src->ptr(sx, sy), palette }, dst, dx, dy, w);
  }

  template<typename Src, typename Dst>
  inline void _span_scale_to(Src s, Dst d, uint32_t dst_alpha, int w, fx16_t sx, fx16_t sx_step, int sw) {
    if(dst_alpha == 255u) span_scale_over<false>(s, d, w, sx, sx_step, sw, 255u);
    else                  span_scale_over<true >(s, d, w, sx, sx_step, sw, dst_alpha);
  }

  template<typename Src, typename Dst>
  inline void _span_bilinear_to(Src r0, Src r1, Dst d, uint32_t dst_alpha, int w, fx16_t sx, fx16_t sx_step, int sw, uint32_t fy) {
    if(dst_alpha == 255u) span_bilinear_over<false>(r0, r1, d, w, sx, sx_step, sw, fy, 255u);
    else                  span_bilinear_over<true >(r0, r1, d, w, sx, sx_step, sw, fy, dst_alpha);
  }

  inline void span_blit_scale(image_t *src, image_t *dst, blend_func_t bf, fx16_t sx, fx16_t sx_step, fx16_t sy, int dx, int dy, int w, filter_t filter = NEAREST) {
    (void)bf;
    const bool d565 = dst->pixel_format() == RGB565;
    if(d565) pv_fence_565();                         // scan-out may still be reading
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

      dst_565  dh{ d565 ? (uint16_t *)dst->ptr(dx, dy) : nullptr };
      dst_rgba dw{ d565 ? nullptr : (uint32_t *)dst->ptr(dx, dy) };
      if(src->has_palette()) {
        src_pal s{ (uint8_t *)src->ptr(0, iy), src->palette_data() };
        if(d565) _span_scale_to(s, dh, dst_alpha, w, sx, sx_step, sw);
        else     _span_scale_to(s, dw, dst_alpha, w, sx, sx_step, sw);
      } else if(src->pixel_format() == RGB565) {
        src_565 s{ (uint16_t *)src->ptr(0, iy) };
        if(d565) _span_scale_to(s, dh, dst_alpha, w, sx, sx_step, sw);
        else     _span_scale_to(s, dw, dst_alpha, w, sx, sx_step, sw);
      } else {
        src_rgba s{ (uint32_t *)src->ptr(0, iy) };
        if(d565) _span_scale_to(s, dh, dst_alpha, w, sx, sx_step, sw);
        else     _span_scale_to(s, dw, dst_alpha, w, sx, sx_step, sw);
      }
      return;
    }

    // BILINEAR: like NEAREST, sy is constant across the span, so resolve both
    // source rows and the vertical fraction once and index them directly.
    if(filter == BILINEAR) {
      rect_t b = src->bounds();
      int sw = (int)b.w;
      int sh = (int)b.h;
      int iy = sy >> 16;
      uint32_t fy = (sy >> 8) & 0xffu;
      int y0 = iy, y1 = iy + 1;
      if(y0 < 0) y0 = 0; else if(y0 >= sh) y0 = sh - 1;
      if(y1 < 0) y1 = 0; else if(y1 >= sh) y1 = sh - 1;

      dst_565  dh{ d565 ? (uint16_t *)dst->ptr(dx, dy) : nullptr };
      dst_rgba dw{ d565 ? nullptr : (uint32_t *)dst->ptr(dx, dy) };
      if(src->pixel_format() == RGB565) {
        src_565 r0{ (uint16_t *)src->ptr(0, y0) };
        src_565 r1{ (uint16_t *)src->ptr(0, y1) };
        if(d565) _span_bilinear_to(r0, r1, dh, dst_alpha, w, sx, sx_step, sw, fy);
        else     _span_bilinear_to(r0, r1, dw, dst_alpha, w, sx, sx_step, sw, fy);
      } else {
        src_rgba r0{ (uint32_t *)src->ptr(0, y0) };
        src_rgba r1{ (uint32_t *)src->ptr(0, y1) };
        if(d565) _span_bilinear_to(r0, r1, dh, dst_alpha, w, sx, sx_step, sw, fy);
        else     _span_bilinear_to(r0, r1, dw, dst_alpha, w, sx, sx_step, sw, fy);
      }
      return;
    }

    // BICUBIC: sample() does the interpolation, so we can't fold the fetch into
    // a raw row index.
    uint16_t *ph = d565 ? (uint16_t *)dst->ptr(dx, dy) : nullptr;
    uint32_t *pd = d565 ? nullptr : (uint32_t *)dst->ptr(dx, dy);
    while(w--) {
      uint32_t c = src->sample(sx, sy, filter);
      if(dst_alpha != 255u) c = _premul_mul_alpha(c, dst_alpha);
      if(d565) { *ph = pv_8888_to_565(blend_over_premul(pv_565_to_8888(*ph), c)); ph++; }
      else     { *pd = blend_over_premul(*pd, c); pd++; }
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
