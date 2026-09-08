#include "../brush.hpp"

namespace picovector {

  // Per-pixel filter brush: adds deterministic grain to the target content behind
  // the shape. Position-dependent: the grain is hashed from (x,y). With a nonzero
  // interval the grain also hashes a time frame (PV_TICKS / interval) so it
  // refreshes every `interval` ms; interval 0 holds a static pattern. See
  // color.cpp for the brush-file layout.

  static int grain(int x, int y, int frame, int amount);
  static inline uint8_t clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v); }

  noise_brush_t::noise_brush_t(int amount, int interval)
    : amount(amount), frame(interval > 0 ? (int)(PV_TICKS / interval) : 0) {}

  void noise_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y;
      pv_px p = (pv_px)target->ptr(x, y);
      for(int w = spans[i].w; w; w--) {
        int d = grain(x, y, frame, amount);
        pv_r(p) = clamp8(pv_r(p) + d); // same delta all channels, leave alpha
        pv_g(p) = clamp8(pv_g(p) + d);
        pv_b(p) = clamp8(pv_b(p) + d);
        p += PV_PX_STEP; x++;
      }
    }
  }

  void noise_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y;
      pv_px p = (pv_px)target->ptr(x, y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward the grained value by coverage so AA edges feather in
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        if(!m) { p += PV_PX_STEP; x++; continue; }
        int d = grain(x, y, frame, amount);
        int nr = clamp8(pv_r(p) + d), ng = clamp8(pv_g(p) + d), nb = clamp8(pv_b(p) + d);
        pv_r(p) = (uint8_t)(pv_r(p) + (((nr - pv_r(p)) * m) >> 8));
        pv_g(p) = (uint8_t)(pv_g(p) + (((ng - pv_g(p)) * m) >> 8));
        pv_b(p) = (uint8_t)(pv_b(p) + (((nb - pv_b(p)) * m) >> 8));
        p += PV_PX_STEP; x++;
      }
    }
  }

  // ── helpers ─────────────────────────────────────────────────────────────────
  static int grain(int x, int y, int frame, int amount) {
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)frame * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    int span = 2 * amount + 1; if(span < 1) span = 1;
    return (int)(h % (unsigned)span) - amount;
  }

}
