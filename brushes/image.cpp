#include "../brush.hpp"

namespace picovector {

  image_brush_t::image_brush_t(image_t *src, filter_t filter) : src(src), filter(filter) {
  }

  image_brush_t::image_brush_t(image_t *src, mat3_t *transform, filter_t filter) : src(src), filter(filter) {
    if(transform) {
      base_inverse = *transform;
      base_inverse.inverse();
    }
    inverse_transform = base_inverse; // no shape transform applied yet
  }

  // Fold the shape's transform into the texture mapping so the image tracks the
  // shape: device->image = (brush's own inverse) * inverse(shape transform).
  void image_brush_t::set_render_transform(mat3_t *transform) {
    if(!transform) { inverse_transform = base_inverse; return; }
    mat3_t inv = *transform;
    inv.inverse();
    inverse_transform = base_inverse;
    inverse_transform.multiply(inv);
  }

  // Reduce a fixed-point source coordinate into [0, n << 16), exactly. Used per
  // span rather than per pixel: the walk stays reduced from there on.
  static inline uint32_t _reduce_fx(fx16_t v, int32_t limit) {
    if(v >= 0 && v < limit) return (uint32_t)v;   // the usual case for a step
    int32_t m = v % limit;
    return (uint32_t)(m < 0 ? m + limit : m);
  }

  // The wrapping counterpart to image_t::sample(), which edge-clamps: the brush
  // tiles, so clamped taps would seam at every tile boundary. Taps go through
  // get_unsafe(), so a palette source interpolates its resolved colours.
  //
  // sx and sy are already reduced into [0, tw << 16) and [0, th << 16) by the
  // caller's walk, so every tap is within a texel or two of the range and wraps
  // by compare rather than by division.
  static inline uint32_t _sample_wrapped(image_t *src, fx16_t sx, fx16_t sy, int tw, int th, filter_t filter) {
    int ix = sx >> 16;
    int iy = sy >> 16;

    if(filter == BILINEAR) {
      uint32_t fx = (sx >> 8) & 0xffu;  // sub-texel fraction 0..255
      uint32_t fy = (sy >> 8) & 0xffu;
      // ix and iy are already inside the tile, so only the +1 taps can wrap.
      int x0 = ix, y0 = iy;
      int x1 = x0 + 1 == tw ? 0 : x0 + 1;
      int y1 = y0 + 1 == th ? 0 : y0 + 1;
      uint32_t c00 = src->get_unsafe(x0, y0), c10 = src->get_unsafe(x1, y0);
      uint32_t c01 = src->get_unsafe(x0, y1), c11 = src->get_unsafe(x1, y1);
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

    // BICUBIC: separable 4x4 Catmull-Rom over premultiplied channels, matching
    // image_t::sample() but wrapped, and reading through get_unsafe() rather
    // than raw rows so palette sources resolve to colours first.
    int wx[4], wy[4];
    cubic_weights_fx((sx >> 4) & 0xfff, wx);  // sx/sy fraction (Q16) -> Q12
    cubic_weights_fx((sy >> 4) & 0xfff, wy);

    // The four taps are consecutive texels, so stepping one index round the
    // tile costs a compare where wrapping each of them separately cost a
    // division. Correct for any tile size, down to a single texel.
    int xs[4];
    for(int k = 0, x = ix ? ix - 1 : tw - 1; k < 4; k++) {
      xs[k] = x;
      if(++x == tw) x = 0;
    }

    int ar = 0, ag = 0, ab = 0, aa = 0;  // vertical accumulators, Q18
    for(int j = 0, y = iy ? iy - 1 : th - 1; j < 4; j++, y = (y + 1 == th) ? 0 : y + 1) {

      int hr = 0, hg = 0, hb = 0, ha = 0;  // horizontal sums, Q12
      for(int i = 0; i < 4; i++) {
        uint32_t c = src->get_unsafe(xs[i], y);
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

    int rr = clamp((ar + (1 << 17)) >> 18, 0, 255);
    int gg = clamp((ag + (1 << 17)) >> 18, 0, 255);
    int bb = clamp((ab + (1 << 17)) >> 18, 0, 255);
    int av = clamp((aa + (1 << 17)) >> 18, 0, 255);
    return (uint32_t)rr | ((uint32_t)gg << 8) | ((uint32_t)bb << 16) | ((uint32_t)av << 24);
  }

  void image_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    image_brush_t *p = this;
    const pv_span *spans = _spans();
    // The texel varies per pixel, so the fold cannot be hoisted the way a solid
    // colour's can - but the test can, leaving the opaque path a plain blend.
    uint32_t alpha = target->alpha();
    // The source does not change across the batch, and the walk needs its size
    // in fixed point.
    rect_t b = p->src->bounds();
    const int tw = int(b.w), th = int(b.h);
    if(tw <= 0 || th <= 0) return;
    const int32_t limx = (int32_t)tw << 16, limy = (int32_t)th << 16;
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      uint32_t *dst = (uint32_t*)target->ptr(x, y);

      fx16_vec2_t p1(x, y);
      fx16_vec2_t p2((x + w), y);

      p1 = p1.transform(&p->inverse_transform);
      p2 = p2.transform(&p->inverse_transform);

      fx16_vec2_t pd((p2.x - p1.x) / w, (p2.y - p1.y) / w);

      // Walk the source coordinate reduced into one tile. The step is reduced
      // with it, which is what makes one compare and subtract enough however
      // far a device pixel moves through the texture, and the sample then needs
      // no modulo at all where it used to need four.
      uint32_t sx = _reduce_fx(p1.x, limx), sy = _reduce_fx(p1.y, limy);
      const uint32_t dx = _reduce_fx(pd.x, limx), dy = _reduce_fx(pd.y, limy);

      if(p->filter == NEAREST) {
        for(int j = 0; j < w; j++) {
          sx += dx; if(sx >= (uint32_t)limx) sx -= (uint32_t)limx;
          sy += dy; if(sy >= (uint32_t)limy) sy -= (uint32_t)limy;
          uint32_t c = p->src->get_unsafe((int)(sx >> 16), (int)(sy >> 16));
          if(alpha != 255u) c = _premul_mul_alpha(c, alpha);
          *dst = blend_over_premul(*dst, c);
          dst++;
        }
      } else {
        for(int j = 0; j < w; j++) {
          sx += dx; if(sx >= (uint32_t)limx) sx -= (uint32_t)limx;
          sy += dy; if(sy >= (uint32_t)limy) sy -= (uint32_t)limy;
          uint32_t c = _sample_wrapped(p->src, (fx16_t)sx, (fx16_t)sy, tw, th, p->filter);
          if(alpha != 255u) c = _premul_mul_alpha(c, alpha);
          *dst = blend_over_premul(*dst, c);
          dst++;
        }
      }
    }
  }

  void image_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    image_brush_t *p = this;
    const pv_masked_span *spans = _masked_spans();
    uint32_t alpha = target->alpha();
    rect_t b = p->src->bounds();
    const int tw = int(b.w), th = int(b.h);
    if(tw <= 0 || th <= 0) return;
    const int32_t limx = (int32_t)tw << 16, limy = (int32_t)th << 16;
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      uint8_t *mask = (uint8_t*)spans[i].mask;
      uint32_t *dst = (uint32_t*)target->ptr(x, y);

      fx16_vec2_t p1(x, y);
      fx16_vec2_t p2((x + w), y);

      p1 = p1.transform(&p->inverse_transform);
      p2 = p2.transform(&p->inverse_transform);

      fx16_vec2_t pd((p2.x - p1.x) / w, (p2.y - p1.y) / w);

      // The reduced walk, as in blend_spans().
      uint32_t sx = _reduce_fx(p1.x, limx), sy = _reduce_fx(p1.y, limy);
      const uint32_t dx = _reduce_fx(pd.x, limx), dy = _reduce_fx(pd.y, limy);

      if(p->filter == NEAREST) {
        for(int j = 0; j < w; j++) {
          // the source position steps whether or not the pixel is covered
          sx += dx; if(sx >= (uint32_t)limx) sx -= (uint32_t)limx;
          sy += dy; if(sy >= (uint32_t)limy) sy -= (uint32_t)limy;
          uint32_t m = *mask++;
          if(m) {
            uint32_t c = p->src->get_unsafe((int)(sx >> 16), (int)(sy >> 16));
            if(alpha != 255u) c = _premul_mul_alpha(c, alpha);
            blend_masked_over_premul(dst, c, m);
          }
          dst++;
        }
      } else {
        for(int j = 0; j < w; j++) {
          sx += dx; if(sx >= (uint32_t)limx) sx -= (uint32_t)limx;
          sy += dy; if(sy >= (uint32_t)limy) sy -= (uint32_t)limy;
          uint32_t m = *mask++;
          if(m) {
            uint32_t c = _sample_wrapped(p->src, (fx16_t)sx, (fx16_t)sy, tw, th, p->filter);
            if(alpha != 255u) c = _premul_mul_alpha(c, alpha);
            blend_masked_over_premul(dst, c, m);
          }
          dst++;
        }
      }
    }
  }

}
