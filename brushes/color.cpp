#include "../brush.hpp"

namespace picovector {


  void color_brush_span_func(image_t *target, brush_t *brush, int x, int y, int w) {
    color_brush_t *p = (color_brush_t*)brush;
    uint32_t *dst = (uint32_t*)target->ptr(x, y);
    uint32_t src = p->c._p;

    if(target->alpha() != 255) {
      src = _premul_mul_alpha(src, target->alpha());
    }

    while(w--) {
      *dst = blend_over_premul(*dst, src);
      dst++;
    }
  }

  void color_brush_masked_span_func(image_t *target, brush_t *brush, int x, int y, int w, uint8_t *mask) {
    color_brush_t *p = (color_brush_t*)brush;
    uint32_t *dst = (uint32_t*)target->ptr(x, y);
    uint32_t src = p->c._p;

    if(target->alpha() != 255) {
      src = _premul_mul_alpha(src, target->alpha());
    }

    while(w--) {
      // fold the coverage mask into the premultiplied colour (SWAR, two channels
      // per multiply) then composite — replaces four per-channel multiplies plus
      // the unpack/indirect-call/repack round trip through the blend pointer.
      *dst = blend_over_premul(*dst, _premul_mul_alpha(src, *mask));
      dst++;
      mask++;
    }
  }

  color_brush_t::color_brush_t(const color_t& c) : c(c) {
  }

  span_func_t color_brush_t::span_func() {
    return color_brush_span_func;
  }

  masked_span_func_t color_brush_t::masked_span_func() {
    return color_brush_masked_span_func;
  }

  // A colour brush fills with one constant word. Force the alpha byte to 255 so
  // the cleared background is opaque (premultiplied layout is 0xAABBGGRR).
  bool color_brush_t::solid_fill(uint32_t &out) {
    out = c._p | 0xff000000;
    return true;
  }

}