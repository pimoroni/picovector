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

  // A target that isn't RGBA8888. Brushes only write RGBA8888, so each span is
  // expanded into a scratch row, drawn there by the brush through a stand-in
  // image (image_t::row_proxy), and packed back, PV_CONVERT_CHUNK pixels at a
  // time. The span buffer entry is narrowed to the chunk for the brush's call
  // and put back after. Single core: the scratch row is on this stack.
  static const int PV_CONVERT_CHUNK = 256;

  static void _blend_spans_converted(image_t *target, brush_t *brush, int n) {
    if(brush->reads_neighbours()) return;
    pv_span *spans = (pv_span *)_span_buf;
    if(color_brush_t *cb = brush->as_color()) {
      // an opaque colour needs nothing read back: the fill, clear and text path
      pixel_t src = fold_target_alpha(target, cb->c);
      if(_a(src) == 255) {
        for(int i = 0; i < n; i++) target->fill_row(spans[i].x, spans[i].y, spans[i].w, src);
        return;
      }
    }
    uint32_t row[PV_CONVERT_CHUNK];
    image_t proxy = target->row_proxy();
    for(int i = 0; i < n; i++) {
      const pv_span s = spans[i];
      for(int off = 0; off < s.w; off += PV_CONVERT_CHUNK) {
        int w = s.w - off < PV_CONVERT_CHUNK ? s.w - off : PV_CONVERT_CHUNK;
        int x = s.x + off;
        target->expand_row(x, s.y, w, row);
        spans[i] = { (int16_t)x, s.y, (uint16_t)w };
        proxy.row_proxy_at(row, x);
        brush->blend_spans(&proxy, i, i + 1, 1);
        target->pack_row(x, s.y, w, row);
      }
      spans[i] = s;
    }
  }

  static void _blend_masked_spans_converted(image_t *target, brush_t *brush, int n) {
    if(brush->reads_neighbours()) return;
    pv_masked_span *spans = (pv_masked_span *)_span_buf;
    uint32_t row[PV_CONVERT_CHUNK];
    image_t proxy = target->row_proxy();
    for(int i = 0; i < n; i++) {
      const pv_masked_span s = spans[i];
      for(int off = 0; off < s.w; off += PV_CONVERT_CHUNK) {
        int w = s.w - off < PV_CONVERT_CHUNK ? s.w - off : PV_CONVERT_CHUNK;
        int x = s.x + off;
        target->expand_row(x, s.y, w, row);
        spans[i] = { (int16_t)x, s.y, (uint16_t)w, s.mask + off };
        proxy.row_proxy_at(row, x);
        brush->blend_masked_spans(&proxy, i, i + 1, 1);
        target->pack_row(x, s.y, w, row);
      }
      spans[i] = s;
    }
  }

  // Blend the shared span buffer with `brush` in one call - dispatches to the
  // brush's batch func. Draw methods call this after filling the buffer. When
  // core1 is available and the batch is large enough, the span list is split
  // across both cores (parity by span index -> disjoint framebuffer rows).
  void _blend_spans(image_t *target, brush_t *brush) {
    // Nothing can write to an indexed image: its bytes are palette indices,
    // not colours. It is a source, not a target.
    if(!target->drawable()) return;
    if(!brush) return;
    int n = _num_spans();
    if(n <= 0) return;
    if(!target->native()) { _blend_spans_converted(target, brush, n); return; }
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
    if(!target->drawable()) return;   // see _blend_spans
    if(!brush) return;
    int n = _num_spans();
    if(n <= 0) return;
    if(!target->native()) { _blend_masked_spans_converted(target, brush, n); return; }
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
