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
      uint8_t *p = (uint8_t*)target->ptr(spans[i].x, spans[i].y);
      for(int w = spans[i].w; w; w--) {
        int lum = luminance(p);
        *(uint32_t*)p = lut[lum];
        p += 4;
      }
    }
  }

  void duotone_brush_t::blend_spans_565(image_t *target, int i0, int i1, int step) {
    uint16_t red[32], green[64], blue[32], out[256];
    for(int i = 0; i < 32; i++) {
      uint8_t r = (uint8_t)((i << 3) | (i >> 2));
      red[i] = (uint16_t)(77 * r);
      blue[i] = (uint16_t)(29 * r);
    }
    for(int i = 0; i < 64; i++) green[i] = (uint16_t)(150 * ((i << 2) | (i >> 4)));
    for(int i = 0; i < 256; i++) out[i] = pv_8888_to_565(lut[i]);
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      uint16_t *d = (uint16_t *)target->ptr(spans[i].x, spans[i].y);
      for(int w = spans[i].w; w; w--, d++) {
        uint16_t p = *d;
        *d = out[(red[p >> 11] + green[(p >> 5) & 0x3fu] + blue[p & 0x1fu]) >> 8];
      }
    }
  }

  void duotone_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      uint8_t *p = (uint8_t*)target->ptr(spans[i].x, spans[i].y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward the ramp colour by coverage so AA edges feather in
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        if(!m) { p += 4; continue; }
        uint32_t tgt = lut[luminance(p)];
        p[0] = (uint8_t)(p[0] + ((((int)(tgt & 0xff)) - p[0]) * m >> 8));
        p[1] = (uint8_t)(p[1] + ((((int)((tgt >> 8) & 0xff)) - p[1]) * m >> 8));
        p[2] = (uint8_t)(p[2] + ((((int)((tgt >> 16) & 0xff)) - p[2]) * m >> 8)); // leave alpha
        p += 4;
      }
    }
  }

}
