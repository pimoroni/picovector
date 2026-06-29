#pragma once

#include <stdint.h>

#include "image.hpp"

namespace picovector {

  void span_blit(image_t *src, image_t *dst, blend_func_t bf, int sx, int sy, int dx, int dy, int w) {
    uint32_t *ps = (uint32_t *)src->ptr(sx, sy);
    uint32_t *pd = (uint32_t *)dst->ptr(dx, dy);
    uint32_t dst_alpha = dst->alpha();

    while(w--) {
      uint32_t c = *ps;
      if(dst_alpha != 255) {
        c = _premul_mul_alpha(c, dst_alpha);
      }
      *pd = bf(*pd, _r(c), _g(c), _b(c), _a(c));
      pd++;
      ps++;
    }
  }

  void span_blit(image_t *src, image_t *dst, blend_func_t bf, int sx, int sy, int dx, int dy, int w, palette_t palette) {
    uint8_t *ps = (uint8_t *)src->ptr(sx, sy);
    uint32_t *pd = (uint32_t *)dst->ptr(dx, dy);
    uint32_t dst_alpha = dst->alpha();

    while(w--) {
      uint32_t c = palette[*ps];
      if(dst_alpha != 255) {
        c = _premul_mul_alpha(c, dst_alpha);
      }
      *pd = bf(*pd, _r(c), _g(c), _b(c), _a(c));
      pd++;
      ps++;
    }
  }

  void span_blit_scale(image_t *src, image_t *dst, blend_func_t bf, fx16_t sx, fx16_t sx_step, fx16_t sy, int dx, int dy, int w, filter_t filter = NEAREST) {
    uint32_t *pd = (uint32_t *)dst->ptr(dx, dy);
    uint32_t src_alpha = src->alpha();

    // NEAREST (and palette images, which sample() always resolves nearest): sy
    // is constant across the span, so the source row is fixed. Resolve it once
    // and index by the integer part of sx — this skips the per-pixel sample()
    // call along with its filter dispatch, bounds reload, iy clamp and y-axis
    // address maths. This is the common sprite/background blit path.
    if(filter == NEAREST || src->has_palette()) {
      int sw = (int)src->bounds().w;
      int sh = (int)src->bounds().h;
      int iy = sy >> 16;
      if(iy < 0) iy = 0; else if(iy >= sh) iy = sh - 1;

      if(src->has_palette()) {
        uint8_t *row = (uint8_t *)src->ptr(0, iy);
        while(w--) {
          int ix = sx >> 16;
          if(ix < 0) ix = 0; else if(ix >= sw) ix = sw - 1;
          uint32_t c = src->palette(row[ix]);
          if(src_alpha != 255) c = _premul_mul_alpha(c, src_alpha);
          *pd = bf(*pd, _r(c), _g(c), _b(c), _a(c));
          pd++;
          sx += sx_step;
        }
      } else {
        uint32_t *row = (uint32_t *)src->ptr(0, iy);
        while(w--) {
          int ix = sx >> 16;
          if(ix < 0) ix = 0; else if(ix >= sw) ix = sw - 1;
          uint32_t c = row[ix];
          if(src_alpha != 255) c = _premul_mul_alpha(c, src_alpha);
          *pd = bf(*pd, _r(c), _g(c), _b(c), _a(c));
          pd++;
          sx += sx_step;
        }
      }
      return;
    }

    while(w--) {
      uint32_t c = src->sample(sx, sy, filter);
      if(src_alpha != 255) {
        c = _premul_mul_alpha(c, src_alpha);
      }
      *pd = bf(*pd, _r(c), _g(c), _b(c), _a(c));
      pd++;
      sx += sx_step;
    }
  }

  // palette images can't be interpolated, so sample() resolves them NEAREST;
  // the palette argument is no longer needed but kept for call-site compatibility
  void span_blit_scale(image_t *src, image_t *dst, blend_func_t bf, fx16_t sx, fx16_t sx_step, fx16_t sy, int dx, int dy, int w, palette_t palette, filter_t filter = NEAREST) {
    (void)palette;
    span_blit_scale(src, dst, bf, sx, sx_step, sy, dx, dy, w, filter);
  }

}