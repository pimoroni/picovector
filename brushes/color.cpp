#include "../brush.hpp"

namespace picovector {

  // ── brush layout (the template every brush in this folder follows) ──────────
  // 1. forward-declare any helper used by more than one method below (a helper
  //    used only once is inlined at its call site instead).
  // 2. constructor
  // 3. blend_spans()        - solid batch
  // 4. blend_masked_spans() - coverage-masked (AA) batch
  // 5. helper bodies

  static uint32_t color_src(image_t *target, brush_t *brush);

  color_brush_t::color_brush_t(const color_t& c) : c(c) {}

  void color_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    uint32_t src = color_src(target, this);
    const pv_span *spans = _spans();
    if(_a(src) == 255) {
      // opaque: a straight copy - hoists blend_over_premul's a==255 early-out out
      // of the pixel loop. This is the clear()/solid-fill fast path.
      for(int i = i0; i < i1; i += step) {
        uint32_t *dst = (uint32_t*)target->ptr(spans[i].x, spans[i].y);
        for(int w = spans[i].w; w; w--) *dst++ = src;
      }
    } else {
      for(int i = i0; i < i1; i += step) {
        uint32_t *dst = (uint32_t*)target->ptr(spans[i].x, spans[i].y);
        for(int w = spans[i].w; w; w--) { *dst = blend_over_premul(*dst, src); dst++; }
      }
    }
  }

  void color_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    uint32_t src = color_src(target, this);
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      uint32_t *dst = (uint32_t*)target->ptr(spans[i].x, spans[i].y);
      const uint8_t *mask = spans[i].mask;
      // fold coverage into the premultiplied colour (SWAR), then composite
      for(int w = spans[i].w; w; w--) { *dst = blend_over_premul(*dst, _premul_mul_alpha(src, *mask)); dst++; mask++; }
    }
  }

  // ── helpers ─────────────────────────────────────────────────────────────────
  // the target's global alpha, folded into the premultiplied pen colour once
  static uint32_t color_src(image_t *target, brush_t *brush) {
    uint32_t src = ((color_brush_t*)brush)->c._p;
    if(target->alpha() != 255) src = _premul_mul_alpha(src, target->alpha());
    return src;
  }

}
