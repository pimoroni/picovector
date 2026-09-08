#include "../brush.hpp"

namespace picovector {

  // Map luminance onto a shadow->highlight two-colour ramp (sepia etc). A LUT of
  // premultiplied packed colours is built in the ctor. See color.cpp for the
  // brush-file layout. Per-pixel filter brush: it reads the target content
  // behind the shape and rewrites it, so there is no pen colour.

  duotone_brush_t::duotone_brush_t(const color_t &shadow, const color_t &highlight) {
    uint32_t s = shadow._p, h = highlight._p;
    for(int i = 0; i < 256; i++) {
      uint32_t out = 0;
      for(int ch = 0; ch < 4; ch++) {
        int sc = (s >> (ch * 8)) & 0xff, hc = (h >> (ch * 8)) & 0xff;
        int v = sc + (hc - sc) * i / 255;
        out |= (uint32_t)v << (ch * 8);
      }
      lut[i] = out;
    }
  }

  void duotone_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      pv_px p = (pv_px)target->ptr(spans[i].x, spans[i].y);
      for(int w = spans[i].w; w; w--) {
        int lum = luminance(p);
        pv_word(p) = lut[lum];
        p += PV_PX_STEP;
      }
    }
  }

  void duotone_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      pv_px p = (pv_px)target->ptr(spans[i].x, spans[i].y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward the ramp colour by coverage so AA edges feather in
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        if(!m) { p += PV_PX_STEP; continue; }
        uint32_t tgt = lut[luminance(p)];
        pv_r(p) = (uint8_t)(pv_r(p) + ((((int)(tgt & 0xff)) - pv_r(p)) * m >> 8));
        pv_g(p) = (uint8_t)(pv_g(p) + ((((int)((tgt >> 8) & 0xff)) - pv_g(p)) * m >> 8));
        pv_b(p) = (uint8_t)(pv_b(p) + ((((int)((tgt >> 16) & 0xff)) - pv_b(p)) * m >> 8)); // leave alpha
        p += PV_PX_STEP;
      }
    }
  }

}
