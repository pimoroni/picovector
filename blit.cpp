// Blitting: image-to-image copies (unscaled, scaled/filtered, and interpolated
// spans), split across both cores via pv_parallel_rows. The per-span pixel cores
// (span_blit / span_blit_scale) live in blit.hpp; this file holds the image_t::blit*
// entry points and their tiling/clipping/dual-core dispatch.

#include <string.h>
#include <math.h>
#include <algorithm>

#include "image.hpp"
#include "blend.hpp"
#include "blit.hpp"
#include "brush.hpp"
#include "picovector.hpp" // pv_parallel_rows (dual-core blit split)

using std::min, std::max;

namespace picovector {

  void clip_blit_rect(rect_t &r1, rect_t b, rect_t &r2) {
    // perform any source rect clipping that's needed
    float sx = r2.w / r1.w;
    float sy = r2.h / r1.h;

    if(r1.x < b.x) {
      float d = b.x - r1.x;
      r1.x += d;
      r1.w -= d;
      r2.x += (d * sx);
      r2.w -= (d * sx);
    }

    if(r1.y < b.y) {
      float d = b.y - r1.y;
      r1.y += d;
      r1.h -= d;
      r2.y += (d * sy);
      r2.h -= (d * sy);
    }

    if(r1.x + r1.w > b.x + b.w) {
      float d = (r1.x + r1.w) - (b.x + b.w);
      r1.w -= d;
      r2.w -= (d * sx);
    }

    if(r1.y + r1.h > b.y + b.h) {
      float d = (r1.y + r1.h) - (b.y + b.h);
      r1.h -= d;
      r2.h -= (d * sy);
    }
  }

#if PV_DUAL_CORE
  namespace {
    // one-to-one (unscaled) blit, a disjoint row parity per core
    struct pv_blit_ctx {
      image_t *src; image_t *dst; blend_func_t bf;
      int srx, sry, trx, try_, trw;
      bool has_palette; const uint32_t *palette;
    };
    void pv_blit_rows(void *v, int y0, int y1, int step) {
      pv_blit_ctx *c = (pv_blit_ctx *)v;
      if(c->has_palette) {
        for(int y = y0; y < y1; y += step)
          span_blit(c->src, c->dst, c->bf, c->srx, c->sry + y, c->trx, c->try_ + y, c->trw, c->palette);
      } else {
        for(int y = y0; y < y1; y += step)
          span_blit(c->src, c->dst, c->bf, c->srx, c->sry + y, c->trx, c->try_ + y, c->trw);
      }
    }

    // scaled/filtered blit. srcy is linear in the row index, so each core derives
    // its own srcy (srcy0 + y*srcstepy) rather than sharing a running accumulator.
    struct pv_blit_scale_ctx {
      image_t *src; image_t *dst; blend_func_t bf;
      int srcx, srcstepx, srcy0, srcstepy, trx, try_, trw;
      filter_t filter;
    };
    void pv_blit_scale_rows(void *v, int y0, int y1, int step) {
      pv_blit_scale_ctx *c = (pv_blit_scale_ctx *)v;
      for(int y = y0; y < y1; y += step) {
        int srcy = c->srcy0 + y * c->srcstepy;
        span_blit_scale(c->src, c->dst, c->bf, c->srcx, c->srcstepx, srcy, c->trx, c->try_ + y, c->trw, c->filter);
      }
    }
  }
#endif

  void image_t::blit(image_t *target, const vec2_t p) {
    // An indexed image is one byte a pixel and every blit writes four; it can be
    // the source of one but never the destination. See brush.cpp.
    if(target->has_palette()) return;
    rect_t sr = _bounds;
    sr = sr.floor();

    rect_t tr(floorf(p.x), floorf(p.y), sr.w, sr.h); // target rect

    clip_blit_rect(sr, _bounds, tr);
    // against the clip, not the bounds: the clip is always within the bounds
    // (the setter intersects), and clip_blit_rect walks the source rect along
    // with it so the visible part still samples the right texels.
    clip_blit_rect(tr, target->_clip, sr);
    if(sr.w <= 0 || sr.h <= 0 || tr.w <= 0 || tr.h <= 0) {
      return;
    }

    blend_func_t bf = target->_blend_func;

#if PV_DUAL_CORE
    // large enough to amortise the inter-core handshake: split rows across cores
    if((int)tr.w * (int)tr.h >= PV_DUAL_CORE_BLIT_MIN_PX) {
      pv_blit_ctx ctx{ this, target, bf, (int)sr.x, (int)sr.y, (int)tr.x, (int)tr.y, (int)tr.w,
                       _has_palette, _palette };
      pv_parallel_rows(pv_blit_rows, &ctx, 0, (int)tr.h);
      return;
    }
#endif

    // _has_palette is fixed for the whole blit, so pick the span variant once
    // rather than re-testing it every scanline.
    if(_has_palette) {
      for(int y = 0; y < tr.h; y++) {
        span_blit(this, target, bf, sr.x, sr.y + y, tr.x, tr.y + y, tr.w, _palette);
      }
    } else {
      for(int y = 0; y < tr.h; y++) {
        span_blit(this, target, bf, sr.x, sr.y + y, tr.x, tr.y + y, tr.w);
      }
    }
  }


  // blit from source rectangle into target rectangle
  void image_t::blit(image_t *target, rect_t sr, rect_t tr, filter_t filter) {
    // An indexed image is one byte a pixel and every blit writes four; it can be
    // the source of one but never the destination. See brush.cpp.
    if(target->has_palette()) return;
    bool flip_h = tr.w < 0;
    bool flip_v = tr.h < 0;

    // clip target rect to target bounds
    // printf("pre clip\n");
    // printf("- sr = %.2f, %.2f (%.2f x %.2f)\n", sr.x, sr.y, sr.w, sr.h);
    // printf("- tr = %.2f, %.2f (%.2f x %.2f)\n", tr.x, tr.y, tr.w, tr.h);
    tr.w = fabs(tr.w);
    tr.h = fabs(tr.h);
    // keep the source rect fractional so sub-texel pan/zoom stays smooth under
    // BILINEAR/BICUBIC (rounding it here snaps the sampled region to whole
    // texels, which steps visibly when magnifying a small source). The dest
    // rect is still snapped to whole pixels — that's the integer span loop.
    tr = tr.round();
    clip_blit_rect(sr, _bounds, tr);
    clip_blit_rect(tr, target->_clip, sr);   // clip, not bounds - see blit(p)
    if(sr.w <= 0 || sr.h <= 0 || tr.w <= 0 || tr.h <= 0) {
      return;
    }
    // printf("post clip\n");
    // printf("- sr = %.2f, %.2f (%.2f x %.2f)\n", sr.x, sr.y, sr.w, sr.h);
    // printf("- tr = %.2f, %.2f (%.2f x %.2f)\n", tr.x, tr.y, tr.w, tr.h);

    blend_func_t bf = target->_blend_func;

    // render the scaled spans
    int srcstepx = (sr.w / tr.w) * 65536.0f;
    int srcstepy = (sr.h / tr.h) * 65536.0f;

    int srcx = sr.x * 65536.0f;
    int srcy = sr.y * 65536.0f;

    if(flip_h) {
      srcstepx = -srcstepx;
      srcx = ((_bounds.w - sr.x) * 65536.0f) + srcstepx;
    }

    if(flip_v) {
      srcstepy = -srcstepy;
      srcy = ((_bounds.h - sr.y) * 65536.0f) + srcstepy;
    }

#if PV_DUAL_CORE
    // scaled/filtered blits are the biggest dual-core win (arithmetic-bound, so
    // closer to a true 2x than a bandwidth-bound copy). Split rows once large.
    if((int)tr.w * (int)tr.h >= PV_DUAL_CORE_BLIT_MIN_PX) {
      pv_blit_scale_ctx ctx{ this, target, bf, srcx, srcstepx, srcy, srcstepy,
                             (int)tr.x, (int)tr.y, (int)tr.w, filter };
      pv_parallel_rows(pv_blit_scale_rows, &ctx, 0, (int)tr.h);
      return;
    }
#endif

    // span_blit_scale resolves palette vs rgba internally via src->has_palette()
    // (the palette overload just forwards), so no per-row dispatch is needed here.
    for(int y = tr.y; y < tr.y + tr.h; y++) {
      span_blit_scale(this, target, bf, srcx, srcstepx, srcy, tr.x, y, tr.w, filter);
      srcy += srcstepy;
    }
  }


  void image_t::blit(image_t *target, rect_t tr, filter_t filter) {
    blit(target, _bounds, tr, filter);
  }

  /*
    blits a span of pixels onto the target image using interpolated samples from
    the source image along a line starting at uv0 and ending at uv1. `vertical`
    picks the travel axis: horizontal (stepping one pixel across a row) or
    vertical (stepping one row down a column). Shared by blit_hspan/blit_vspan.
  */
  void image_t::blit_span(image_t *target, vec2_t p, float len, vec2_t uv0, vec2_t uv1, filter_t filter, bool vertical) {
    // An indexed image is one byte a pixel and every blit writes four; it can be
    // the source of one but never the destination. See brush.cpp.
    if(target->has_palette()) return;
    if(len <= 0.0f) return; // degenerate span (also guards the /len below)
    rect_t b = target->_clip;

    // Full-precision per-pixel texture step (texture units per screen pixel).
    // Kept as float so the start offset below does not inherit the Q16 truncation
    // of the integer step; the inner loop advances the rounded Q16 copy.
    float udf = (uv1.x - uv0.x) / len;
    float vdf = (uv1.y - uv0.y) / len;

    // The span sits on one row (horizontal) or one column (vertical); if that
    // line is outside the clip there is nothing to draw. Without this the span
    // was clipped along its travel axis only, so a row past the bottom of the
    // image still drew - off the end of the buffer.
    float across = vertical ? p.x : p.y;
    float ao = vertical ? b.x : b.y;
    float as = vertical ? b.w : b.h;
    if(across < ao || across >= ao + as) return;

    // clip against the travel axis (y for a vertical span, x for a horizontal one)
    float pp = vertical ? p.y : p.x;
    float bo = vertical ? b.y : b.x;
    float bs = vertical ? b.h : b.w;
    float pp0 = pp; // true (unclipped, fractional) span start

    if(pp < bo) { len -= (bo - pp); pp = bo; }        // leading edge
    // trailing edge: the span must end at the clip's far edge (bo + bs), so the
    // remaining length is (bo + bs) - pp. (The old `bs - pp` only matched this
    // when the clip origin bo was 0 - a non-zero clip.x/y cut spans short.)
    if(pp + len > bo + bs) len = bo + bs - pp;
    if(len <= 0.0f) return;                            // fully clipped

    if(vertical) p.y = pp; else p.x = pp;
    int R = (int)pp;  // first drawn row/col: ptr() truncates p.x/p.y to this
    int n = (int)len; // pixel count

    // Exact texture coords for the first drawn pixel (screen row/col R), derived
    // from the true fractional start pp0 in a single float expression. This is
    // what kills the stair-stepping on near walls: the old code advanced a
    // truncated Q16 step by the large off-screen top offset (bo - pp0), so the
    // step's ~0.5% error was amplified into a per-column texel misregistration
    // that stepped as the step itself stepped. `off` is the screen distance from
    // pp0 to this pixel; the +1 preserves the historic one-step-in sampling phase
    // (pixel-aligned blits unchanged to <1 LSB). ud/vd are rounded, not
    // truncated, to halve the residual per-span drift.
    float off = (float)(R + 1) - pp0;
    // Per-pixel step and start carried in int64 with 32 fractional bits (vs
    // Q16.16's 16). vd is a sub-unit quantity, so quantising it to whole Q16.16
    // LSBs leaves a ~0.5% error that differs per column - enough to still
    // stair-step tall spans toward the bottom once the start is exact. The extra
    // fractional bits remove that. The coord is a 2^32-scaled fraction, so the low
    // 32 bits are the [0,1) texture position (and wrap, like the old & 0xffff).
    const float Q32 = 4294967296.0f; // 2^32
    int64_t ud = (int64_t)(udf * Q32);
    int64_t vd = (int64_t)(vdf * Q32);
    int64_t u = (int64_t)((uv0.x + udf * off) * Q32) - ud;
    int64_t v = (int64_t)((uv0.y + vdf * off) * Q32) - vd;

    uint32_t tw = int(this->_bounds.w - 1);
    uint32_t th = int(this->_bounds.h - 1);

    // one pixel across a row, or one row (stride) down a column
    int dst_step = vertical ? (target->_row_stride >> 2) : 1;

    uint32_t *dst = (uint32_t *)target->ptr(p.x, p.y);

    // Palette images always resolve NEAREST (indices can't be interpolated).
    if(filter == NEAREST || this->_has_palette) {
      // NEAREST fast path: fetch the nearest texel inline via get_unsafe,
      // skipping the per-pixel out-of-line sample() call (its filter dispatch,
      // bounds reload and clamps). ix/iy are already in range by construction -
      // (uv fraction) * (dim - 1) >> 16 lies in [0, dim - 1] - so no clamp is
      // needed, matching sample()'s NEAREST result exactly. The invariant
      // global-alpha test is hoisted out of the loop.
      if(this->_alpha != 255) {
        uint32_t ga = this->_alpha;
        for(int i = 0; i < n; i++) {
          u += ud; v += vd;
          int ix = (int)(((uint64_t)(uint32_t)u * tw) >> 32);
          int iy = (int)(((uint64_t)(uint32_t)v * th) >> 32);
          uint32_t col = _premul_mul_alpha(this->get_unsafe(ix, iy), ga);
          *dst = blend_over_premul(*dst, col);
          dst += dst_step;
        }
      } else {
        for(int i = 0; i < n; i++) {
          u += ud; v += vd;
          int ix = (int)(((uint64_t)(uint32_t)u * tw) >> 32);
          int iy = (int)(((uint64_t)(uint32_t)v * th) >> 32);
          *dst = blend_over_premul(*dst, this->get_unsafe(ix, iy));
          dst += dst_step;
        }
      }
      return;
    }

    // Filtered (BILINEAR/BICUBIC) path: sample() does the interpolation, so we
    // can't fold the fetch into a raw texel index. Alpha test still hoisted.
    if(this->_alpha != 255) {
      uint32_t ga = this->_alpha;
      for(int i = 0; i < n; i++) {
        u += ud; v += vd;
        fx16_t sx = (fx16_t)(((uint64_t)(uint32_t)u * tw) >> 16);
        fx16_t sy = (fx16_t)(((uint64_t)(uint32_t)v * th) >> 16);
        uint32_t col = _premul_mul_alpha(this->sample(sx, sy, filter), ga);
        *dst = blend_over_premul(*dst, col);
        dst += dst_step;
      }
    } else {
      for(int i = 0; i < n; i++) {
        u += ud; v += vd;
        fx16_t sx = (fx16_t)(((uint64_t)(uint32_t)u * tw) >> 16);
        fx16_t sy = (fx16_t)(((uint64_t)(uint32_t)v * th) >> 16);
        *dst = blend_over_premul(*dst, this->sample(sx, sy, filter));
        dst += dst_step;
      }
    }
  }

  void image_t::blit_hspan(image_t *target, vec2_t p, float len, vec2_t uv0, vec2_t uv1, filter_t filter) {
    blit_span(target, p, len, uv0, uv1, filter, false);
  }

  void image_t::blit_vspan(image_t *target, vec2_t p, float len, vec2_t uv0, vec2_t uv1, filter_t filter) {
    blit_span(target, p, len, uv0, uv1, filter, true);
  }


  // --- source sampling (used by the scaled/filtered blit paths) --------------
  // Catmull-Rom (a = -0.5) cubic convolution weights for the four taps either
  // side of a sample point at fractional offset t. Fixed-point: t and the
  // returned weights are Q12; the weights sum to 1.0 (4096). Integer MACs are
  // cheaper than float on the M33 and skip the per-channel int<->float casts.
  static inline void _cubic_weights_fx(int t, int w[4]) {
    int t2 = (t * t) >> 12;
    int t3 = (t2 * t) >> 12;
    w[0] = (-t3 + 2 * t2 - t) >> 1;
    w[1] = (3 * t3 - 5 * t2 + 8192) >> 1;  // 8192 == 2.0 in Q12
    w[2] = (-3 * t3 + 4 * t2 + t) >> 1;
    w[3] = (t3 - t2) >> 1;
  }

  uint32_t image_t::sample(fx16_t sx, fx16_t sy, filter_t filter) {
    int w = (int)this->_bounds.w;
    int h = (int)this->_bounds.h;
    int ix = sx >> 16;
    int iy = sy >> 16;

    // palette indices can't be interpolated, so always sample nearest for them
    if(filter == NEAREST || this->_has_palette) {
      if(ix < 0) ix = 0; else if(ix >= w) ix = w - 1;
      if(iy < 0) iy = 0; else if(iy >= h) iy = h - 1;
      return get_unsafe(ix, iy);
    }

    if(filter == BILINEAR) {
      uint32_t fx = (sx >> 8) & 0xffu;  // sub-texel fraction 0..255
      uint32_t fy = (sy >> 8) & 0xffu;
      int x0 = ix, x1 = ix + 1;
      int y0 = iy, y1 = iy + 1;
      if(x0 < 0) x0 = 0; else if(x0 >= w) x0 = w - 1;
      if(x1 < 0) x1 = 0; else if(x1 >= w) x1 = w - 1;
      if(y0 < 0) y0 = 0; else if(y0 >= h) y0 = h - 1;
      if(y1 < 0) y1 = 0; else if(y1 >= h) y1 = h - 1;
      uint32_t c00 = get_unsafe(x0, y0), c10 = get_unsafe(x1, y0);
      uint32_t c01 = get_unsafe(x0, y1), c11 = get_unsafe(x1, y1);
      uint32_t out = 0;
      for(int s = 0; s < 32; s += 8) {
        int a = (c00 >> s) & 0xff, b = (c10 >> s) & 0xff;
        int c = (c01 >> s) & 0xff, d = (c11 >> s) & 0xff;
        int top = a + (((b - a) * (int)fx) >> 8);
        int bot = c + (((d - c) * (int)fx) >> 8);
        int val = top + (((bot - top) * (int)fy) >> 8);
        out |= ((uint32_t)val) << s;
      }
      return out;
    }

    // BICUBIC: separable 4x4 Catmull-Rom over premultiplied channels. Only
    // reached for non-palette images, so we read framebuffer words directly via
    // per-row pointers (skips get_unsafe's palette branch + per-texel address
    // maths) and accumulate in fixed-point.
    int wx[4], wy[4];
    _cubic_weights_fx((sx >> 4) & 0xfff, wx);  // sx/sy fraction (Q16) -> Q12
    _cubic_weights_fx((sy >> 4) & 0xfff, wy);

    // clamp the four source columns once; reused for every row
    int xs[4];
    for(int k = 0; k < 4; k++) {
      int x = ix - 1 + k;
      xs[k] = x < 0 ? 0 : (x >= w ? w - 1 : x);
    }

    int ar = 0, ag = 0, ab = 0, aa = 0;  // vertical accumulators, Q18
    for(int j = 0; j < 4; j++) {
      int y = iy - 1 + j;
      if(y < 0) y = 0; else if(y >= h) y = h - 1;
      const uint32_t *row = (const uint32_t *)ptr(0, y);

      int hr = 0, hg = 0, hb = 0, ha = 0;  // horizontal sums, Q12
      for(int i = 0; i < 4; i++) {
        uint32_t c = row[xs[i]];
        int wi = wx[i];
        hr += wi * (int)(c & 0xff);
        hg += wi * (int)((c >> 8) & 0xff);
        hb += wi * (int)((c >> 16) & 0xff);
        ha += wi * (int)((c >> 24) & 0xff);
      }
      // >>6 drops Q12->Q6 so the Q12 vertical weight can't overflow int32
      int wj = wy[j];
      ar += wj * (hr >> 6);
      ag += wj * (hg >> 6);
      ab += wj * (hb >> 6);
      aa += wj * (ha >> 6);
    }

    // accumulators are Q18 (Q12 vertical * Q6 horizontal); round and clamp
    int rr = (ar + (1 << 17)) >> 18;
    int gg = (ag + (1 << 17)) >> 18;
    int bb = (ab + (1 << 17)) >> 18;
    int av = (aa + (1 << 17)) >> 18;
    if(rr < 0) rr = 0; else if(rr > 255) rr = 255;
    if(gg < 0) gg = 0; else if(gg > 255) gg = 255;
    if(bb < 0) bb = 0; else if(bb > 255) bb = 255;
    if(av < 0) av = 0; else if(av > 255) av = 255;
    return (uint32_t)rr | ((uint32_t)gg << 8) | ((uint32_t)bb << 16) | ((uint32_t)av << 24);
  }
}
