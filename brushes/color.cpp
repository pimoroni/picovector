#include "../brush.hpp"

namespace picovector {

  // ── brush layout (the template every brush in this folder follows) ──────────
  // 1. forward-declare any helper used by more than one method below (a helper
  //    used only once is inlined at its call site instead).
  // 2. constructor
  // 3. blend_spans()        - solid batch
  // 4. blend_masked_spans() - coverage-masked (AA) batch
  // 5. helper bodies

  static pixel_t color_src(image_t *target, brush_t *brush);

  color_brush_t::color_brush_t(const color_t& c) : c(c._p) {}

  void color_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    pixel_t src = color_src(target, this);
    const pv_span *spans = _spans();
    if(_a(src) == 255) {
      // opaque: a straight copy - hoists blend_over_premul's a==255 early-out out
      // of the pixel loop. This is the clear()/solid-fill fast path.
      for(int i = i0; i < i1; i += step) {
        pv_store_t *dst = (pv_store_t*)target->ptr(spans[i].x, spans[i].y);
        pv_fill(dst, src, spans[i].w);
      }
    } else {
      for(int i = i0; i < i1; i += step) {
        pv_store_t *dst = (pv_store_t*)target->ptr(spans[i].x, spans[i].y);
        for(int w = spans[i].w; w; w--) { pv_blend_over(dst, src); dst++; }
      }
    }
  }

  // Fold coverage into the premultiplied colour (SWAR), then composite. Uncovered
  // pixels are left alone - a span runs from the first to the last covered pixel
  // on its row, so a hollow shape (an arc, a ring, a stroked outline) leaves most
  // of that run at zero. The opaque test is hoisted like blend_spans' is, which
  // makes a fully covered pixel a plain store.
  void color_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    pixel_t src = color_src(target, this);
    const pv_masked_span *spans = _masked_spans();
    bool opaque = _a(src) == 255;
    for(int i = i0; i < i1; i += step) {
      pv_store_t *dst = (pv_store_t*)target->ptr(spans[i].x, spans[i].y);
      const uint8_t *mask = spans[i].mask;
      if(opaque) {
        pv_store_t s = pv_pack(src);
        for(int w = spans[i].w; w; w--, dst++, mask++) {
          uint32_t m = *mask;
          if(m == 0u) continue;
          if(m == 255u) { *dst = s; continue; }
          pv_blend_over(dst, _premul_mul_alpha(src, m));
        }
      } else {
        for(int w = spans[i].w; w; w--, dst++, mask++) {
          uint32_t m = *mask;
          if(m == 0u) continue;
          pv_blend_over(dst, _premul_mul_alpha(src, m));
        }
      }
    }
  }

  // ── helpers ─────────────────────────────────────────────────────────────────
  // the target's global alpha, folded into the premultiplied pen colour once
  static pixel_t color_src(image_t *target, brush_t *brush) {
    return fold_target_alpha(target, ((color_brush_t*)brush)->c);
  }

}
