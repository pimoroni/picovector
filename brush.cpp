#include "types.hpp"
#include "blend.hpp"

#include "brush.hpp"

namespace picovector {

#if PV_DUAL_CORE
  // Adapter so pv_parallel_rows can drive a brush's ranged batch method: the
  // "row" range it hands out is really a span-index range, split by parity.
  struct _blend_ctx { image_t *target; brush_t *brush; bool masked; };
  static void _blend_row_worker(void *ctx, int i0, int i1, int step) {
    _blend_ctx *c = (_blend_ctx *)ctx;
    if(c->masked) c->brush->blend_masked_spans(c->target, i0, i1, step);
    else          c->brush->blend_spans(c->target, i0, i1, step);
  }

  // Total pixels across the current span batch (solid stride) - used to decide
  // whether a blend is big enough to amortise the inter-core handshake.
  static inline int _solid_span_pixels(int n) {
    const pv_span *s = _spans();
    int px = 0;
    for(int i = 0; i < n; i++) px += s[i].w;
    return px;
  }
  static inline int _masked_span_pixels(int n) {
    const pv_masked_span *s = _masked_spans();
    int px = 0;
    for(int i = 0; i < n; i++) px += s[i].w;
    return px;
  }
#endif

  // ---- RGB565 staging --------------------------------------------------------
  // A 565 target (the platform framebuffer) is composited through an RGBA
  // scratch row per core: unpack the span's pixels, run the brush's own batch method
  // against a stand-in RGBA image whose ptr(span.x, span.y) lands on the
  // scratch, pack the result back. Every brush - and anything it reads from
  // the span buffers - works unchanged and never learns the format exists.
  // Chunked to the scratch width, adjusting the span in place and restoring it.
  static uint32_t _stage565[2][PV_RGB565_STAGE_PX];
  uint32_t *pv_rgb565_stage() { return _stage565[0]; }

  template <typename SPAN>
  static void _blend_spans_565(image_t *target, brush_t *brush, int i0, int i1, int step, bool masked, uint32_t *scratch) {
    SPAN *sp = (SPAN *)_span_buf;
    image_t stage(scratch, (int)target->bounds().w, (int)target->bounds().h);
    stage.alpha(target->alpha());
    const size_t bpp = 4;
    for(int i = i0; i < i1; i += step) {
      SPAN &s = sp[i];
      const SPAN saved = s;
      while(s.w > 0) {
        uint16_t chunk = s.w > PV_RGB565_STAGE_PX ? PV_RGB565_STAGE_PX : s.w;
        uint16_t rest = s.w - chunk;
        s.w = chunk;
        uint16_t *d = (uint16_t *)target->ptr(s.x, s.y);
        for(int k = 0; k < chunk; k++) scratch[k] = pv_565_to_8888(d[k]);
        // slide the scratch under (s.x, s.y): stage.ptr(s.x, s.y) == scratch
        stage.rebase_buffer((uint8_t *)scratch - (size_t)s.y * stage.row_stride() - (size_t)s.x * bpp);
        if(masked) brush->blend_masked_spans(&stage, i, i + 1, 1);
        else       brush->blend_spans(&stage, i, i + 1, 1);
        for(int k = 0; k < chunk; k++) d[k] = pv_8888_to_565(scratch[k]);
        if(!rest) break;
        s.x += chunk;
        s.w = rest;
        if constexpr (sizeof(SPAN) == sizeof(pv_masked_span)) ((pv_masked_span &)s).mask += chunk;
      }
      s = saved;
    }
  }

  template <typename SPAN>
  static void _blend_565_range(image_t *target, brush_t *brush, int i0, int i1, int step, bool masked) {
    _blend_spans_565<SPAN>(target, brush, i0, i1, step, masked, _stage565[i0 & 1]);
  }

#if PV_DUAL_CORE
  static void _blend_565_row_worker(void *ctx, int i0, int i1, int step) {
    _blend_ctx *c = (_blend_ctx *)ctx;
    if(c->masked) _blend_565_range<pv_masked_span>(c->target, c->brush, i0, i1, step, true);
    else          _blend_565_range<pv_span>(c->target, c->brush, i0, i1, step, false);
  }
#endif

#if PV_DUAL_CORE
  static void _native_565_row_worker(void *ctx, int i0, int i1, int step) {
    _blend_ctx *c = (_blend_ctx *)ctx;
    c->brush->blend_spans_565(c->target, i0, i1, step);
  }
#endif

  static void _blend_native_565(image_t *target, brush_t *brush, int n) {
#if PV_DUAL_CORE
    if(n >= 2 && _solid_span_pixels(n) >= PV_DUAL_CORE_BLEND_MIN_PX) {
      _blend_ctx c = { target, brush, false };
      pv_parallel_rows(_native_565_row_worker, &c, 0, n);
      return;
    }
#endif
    brush->blend_spans_565(target, 0, n, 1);
  }

  template <typename SPAN>
  static void _blend_565(image_t *target, brush_t *brush, int n, bool masked) {
#if PV_DUAL_CORE
    int px = masked ? _masked_span_pixels(n) : _solid_span_pixels(n);
    if(n >= 2 && px >= PV_DUAL_CORE_BLEND_MIN_PX) {
      _blend_ctx c = { target, brush, masked };
      pv_parallel_rows(_blend_565_row_worker, &c, 0, n);
      return;
    }
#endif
    _blend_565_range<SPAN>(target, brush, 0, n, 1, masked);
  }

  // Blend the shared span buffer with `brush` in one call - dispatches to the
  // brush's batch func. Draw methods call this after filling the buffer. When
  // core1 is available and the batch is large enough, the span list is split
  // across both cores (parity by span index -> disjoint framebuffer rows).
  void _blend_spans(image_t *target, brush_t *brush) {
    // Nothing can write to an indexed image: every brush and filter stores a
    // four-byte pixel, which would run four times past the end of each row of
    // a one-byte-per-pixel buffer. It is a source, not a target.
    if(target->has_palette()) return;
    if(!brush) return;
    int n = _num_spans();
    if(n <= 0) return;
    if(target->pixel_format() == RGB565) {           // staged: see above
      if(brush->samples_neighbourhood()) return;     // scratch row can't feed it
      pv_fence_565();                                // scan-out may still be reading
      pixel_t solid;
      if(brush->solid_opaque(target, solid)) {
        // clear() and solid fills: write the packed colour straight to the
        // framebuffer, word-doubled, instead of staging every span
        const uint16_t p16 = pv_8888_to_565(solid);
        const uint32_t p32 = (uint32_t)p16 | ((uint32_t)p16 << 16);
        const pv_span *sp = _spans();
        for(int i = 0; i < n; i++) {
          uint16_t *d = (uint16_t *)target->ptr(sp[i].x, sp[i].y);
          int w = sp[i].w;
          if(w && ((uintptr_t)d & 2)) { *d++ = p16; w--; }      // align to a word
          uint32_t *d32 = (uint32_t *)d;
          for(; w >= 2; w -= 2) *d32++ = p32;
          if(w) *(uint16_t *)d32 = p16;
        }
        return;
      }
      if(brush->blends_565()) _blend_native_565(target, brush, n);
      else                    _blend_565<pv_span>(target, brush, n, false);
      return;
    }
#if PV_DUAL_CORE
    if(n >= 2 && _solid_span_pixels(n) >= PV_DUAL_CORE_BLEND_MIN_PX) {
      _blend_ctx c = { target, brush, false };
      pv_parallel_rows(_blend_row_worker, &c, 0, n);
      return;
    }
#endif
    brush->blend_spans(target, 0, n, 1);
  }

  void _blend_masked_spans(image_t *target, brush_t *brush) {
    // Nothing can write to an indexed image: every brush and filter stores a
    // four-byte pixel, which would run four times past the end of each row of
    // a one-byte-per-pixel buffer. It is a source, not a target.
    if(target->has_palette()) return;
    if(!brush) return;
    int n = _num_spans();
    if(n <= 0) return;
    if(target->pixel_format() == RGB565) {           // staged: see above
      if(brush->samples_neighbourhood()) return;     // scratch row can't feed it
      pv_fence_565();                                // scan-out may still be reading
      _blend_565<pv_masked_span>(target, brush, n, true);
      return;
    }
#if PV_DUAL_CORE
    if(n >= 2 && _masked_span_pixels(n) >= PV_DUAL_CORE_BLEND_MIN_PX) {
      _blend_ctx c = { target, brush, true };
      pv_parallel_rows(_blend_row_worker, &c, 0, n);
      return;
    }
#endif
    brush->blend_masked_spans(target, 0, n, 1);
  }

}
