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
