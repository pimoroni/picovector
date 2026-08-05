#include "../brush.hpp"

namespace picovector {

  transparent_brush_t::transparent_brush_t() : tint(0) {}
  transparent_brush_t::transparent_brush_t(const color_t &c) : tint(c._p) {}

  // Full coverage: the pixel becomes the tint outright (lerp at coverage 1).
  // For the default transparent tint this writes 0 (erase); used by clear()'s slow
  // path and image_t::rectangle(). The fast clear path uses solid_fill() below.
  void transparent_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    transparent_brush_t *p = this;
    const pv_span *spans = _spans();
    // This brush lerps toward the tint rather than compositing over it, so the
    // target's global alpha weights the lerp - folding it into the tint would
    // darken the colour instead of weakening the erase.
    uint32_t alpha = target->alpha();
    if(alpha == 0u) return;   // exact endpoint: the lerp divides by 256, so it
                              // would shed a bit off an untouched pixel
    uint32_t s = p->tint;
    uint32_t Srb = s & 0x00ff00ffu, Sga = (s >> 8) & 0x00ff00ffu;
    uint32_t inv = 255u - alpha;
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      uint32_t *dst = (uint32_t *)target->ptr(x, y);
      if(alpha == 255u) { while(w--) { *dst++ = s; } continue; }
      while(w--) {
        uint32_t D = *dst;
        uint32_t Drb = D & 0x00ff00ffu, Dga = (D >> 8) & 0x00ff00ffu;
        uint32_t rb = ((Drb * inv + Srb * alpha + 0x00800080u) >> 8) & 0x00ff00ffu;
        uint32_t ga = ((Dga * inv + Sga * alpha + 0x00800080u) >> 8) & 0x00ff00ffu;
        *dst++ = rb | (ga << 8);
      }
    }
  }

  // Coverage lerp toward the tint: dst = lerp(dst, tint, coverage), premultiplied.
  // Because it blends against dst, AA edges feather into the background instead of
  // haloing (as a source-copy would). Endpoints are exact — zero coverage leaves
  // dst untouched, full coverage becomes the tint — and the middle uses the
  // two-lane SWAR weighted sum. With tint == 0 this reduces to dst*(255-cov)/256,
  // bit-identical to the previous destination-out erase.
  void transparent_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    // The target's global alpha scales coverage, so a partial alpha is a partial
    // erase. At 255 the coverage is untouched and the endpoints stay exact.
    uint32_t alpha = target->alpha();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      uint8_t *mask = (uint8_t*)spans[i].mask;
      uint32_t *dst = (uint32_t *)target->ptr(x, y);
      uint32_t S = ((transparent_brush_t *)this)->tint;
      uint32_t Srb = S & 0x00ff00ffu, Sga = (S >> 8) & 0x00ff00ffu;
      while(w--) {
        uint32_t cov = *mask++;
        if(alpha != 255u) cov = (cov * alpha + 127u) / 255u;
        if(cov == 0u)   { dst++; continue; }     // outside the shape: leave dst exact
        if(cov == 255u) { *dst++ = S; continue; } // fully inside: becomes the tint exact
        uint32_t D = *dst;
        uint32_t inv = 255u - cov;
        uint32_t Drb = D & 0x00ff00ffu, Dga = (D >> 8) & 0x00ff00ffu;
        uint32_t rb = ((Drb * inv + Srb * cov + 0x00800080u) >> 8) & 0x00ff00ffu;
        uint32_t ga = ((Dga * inv + Sga * cov + 0x00800080u) >> 8) & 0x00ff00ffu;
        *dst++ = rb | (ga << 8);
      }
    }
  }

  // Solid tint fill lets clear() write the buffer to the tint directly (0 for the
  // default transparent brush, a translucent/opaque word for a colour tint).
}
