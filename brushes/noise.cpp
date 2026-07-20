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
      uint8_t *p = (uint8_t*)target->ptr(x, y);
      for(int w = spans[i].w; w; w--) {
        int d = grain(x, y, frame, amount);
        p[0] = clamp8(p[0] + d); // same delta all channels, leave alpha
        p[1] = clamp8(p[1] + d);
        p[2] = clamp8(p[2] + d);
        p += 4; x++;
      }
    }
  }

  void noise_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y;
      uint8_t *p = (uint8_t*)target->ptr(x, y);
      const uint8_t *mask = spans[i].mask;
      // ease each channel toward the grained value by coverage so AA edges feather in
      for(int w = spans[i].w; w; w--) {
        int d = grain(x, y, frame, amount), m = *mask++;
        int nr = clamp8(p[0] + d), ng = clamp8(p[1] + d), nb = clamp8(p[2] + d);
        p[0] = (uint8_t)(p[0] + (((nr - p[0]) * m) >> 8));
        p[1] = (uint8_t)(p[1] + (((ng - p[1]) * m) >> 8));
        p[2] = (uint8_t)(p[2] + (((nb - p[2]) * m) >> 8));
        p += 4; x++;
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
