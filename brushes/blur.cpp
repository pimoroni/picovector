#include "../brush.hpp"

namespace picovector {

  // blend src over dst by an 8-bit coverage mask (straight per-channel lerp)
  static inline uint32_t blur_mask_lerp(uint32_t dst, uint32_t src, uint32_t m) {
    uint8_t *d = (uint8_t*)&dst;
    uint8_t *s = (uint8_t*)&src;
    uint32_t out;
    uint8_t *o = (uint8_t*)&out;
    o[0] = d[0] + (((int)s[0] - d[0]) * (int)m >> 8);
    o[1] = d[1] + (((int)s[1] - d[1]) * (int)m >> 8);
    o[2] = d[2] + (((int)s[2] - d[2]) * (int)m >> 8);
    o[3] = d[3] + (((int)s[3] - d[3]) * (int)m >> 8);
    return out;
  }

  // (2*radius+1)^2 box average of the target around (px, py), clamped to bounds
  static inline uint32_t box_average(image_t *target, int px, int py, int r, int W, int H) {
    int x0 = px - r < 0 ? 0 : px - r;
    int x1 = px + r >= W ? W - 1 : px + r;
    int y0 = py - r < 0 ? 0 : py - r;
    int y1 = py + r >= H ? H - 1 : py + r;

    uint32_t sr = 0, sg = 0, sb = 0, sa = 0, n = 0;
    for(int yy = y0; yy <= y1; yy++) {
      uint8_t *row = (uint8_t*)target->ptr(x0, yy);
      for(int xx = x0; xx <= x1; xx++) {
        sr += row[0]; sg += row[1]; sb += row[2]; sa += row[3];
        row += 4;
        n++;
      }
    }

    uint32_t out;
    uint8_t *o = (uint8_t*)&out;
    o[0] = (uint8_t)(sr / n);
    o[1] = (uint8_t)(sg / n);
    o[2] = (uint8_t)(sb / n);
    o[3] = (uint8_t)(sa / n);
    return out;
  }

  // tile-width chunk: compute averages into a temp before writing back so a span
  // doesn't blur with its own freshly-written pixels (horizontal feedback)
  #define BLUR_CHUNK 64
  // radius the running-sum fast path handles (bounds the on-stack column buffers);
  // larger radii fall back to the naive box_average (correct, just slower).
  #define BLUR_MAX_R 32

  // Fill out[0..n) with the box-average of the target for pixels (x..x+n-1, y),
  // radius r, using a horizontal running sum: each column's vertical sum is
  // computed once, then a (2r+1)-wide window slides across the row. This is
  // O(n·r) instead of box_average's O(n·r²), and byte-identical to it (it sums the
  // same clamped box, column-first). Requires r <= BLUR_MAX_R. Reads target only,
  // so it stays safe to run per-span across both cores.
  static void blur_row_averages(image_t *target, int x, int y, int n, int r, int W, int H, uint32_t *out) {
    int y0 = y - r < 0 ? 0 : y - r;
    int y1 = y + r >= H ? H - 1 : y + r;
    int ny = y1 - y0 + 1;

    // per-column vertical sums for columns [x-r, x+n-1+r]; off-image columns are 0.
    // indexed by j = c - (x - r), j in [0, n + 2r)
    uint32_t csr[BLUR_CHUNK + 2 * BLUR_MAX_R], csg[BLUR_CHUNK + 2 * BLUR_MAX_R];
    uint32_t csb[BLUR_CHUNK + 2 * BLUR_MAX_R], csa[BLUR_CHUNK + 2 * BLUR_MAX_R];
    int ncol = n + 2 * r;
    for(int j = 0; j < ncol; j++) {
      int c = x - r + j;
      if(c < 0 || c >= W) { csr[j] = csg[j] = csb[j] = csa[j] = 0; continue; }
      uint32_t sr = 0, sg = 0, sb = 0, sa = 0;
      for(int yy = y0; yy <= y1; yy++) {
        uint8_t *px = (uint8_t*)target->ptr(c, yy);
        sr += px[0]; sg += px[1]; sb += px[2]; sa += px[3];
      }
      csr[j] = sr; csg[j] = sg; csb[j] = sb; csa[j] = sa;
    }

    // slide a 2r+1 column window: pixel i's box is columns [i, i+2r] in j-space
    uint32_t br = 0, bg = 0, bb = 0, ba = 0;
    for(int j = 0; j <= 2 * r; j++) { br += csr[j]; bg += csg[j]; bb += csb[j]; ba += csa[j]; }
    for(int i = 0; i < n; i++) {
      int px = x + i;
      int xa = px - r < 0 ? 0 : px - r;
      int xb = px + r >= W ? W - 1 : px + r;
      uint32_t nn = (uint32_t)(ny * (xb - xa + 1)); // valid pixels in the clamped box
      uint32_t o; uint8_t *ob = (uint8_t*)&o;
      ob[0] = (uint8_t)(br / nn); ob[1] = (uint8_t)(bg / nn);
      ob[2] = (uint8_t)(bb / nn); ob[3] = (uint8_t)(ba / nn);
      out[i] = o;
      if(i + 1 < n) { // advance window: drop column i, add column i+2r+1
        br += csr[i + 2 * r + 1] - csr[i]; bg += csg[i + 2 * r + 1] - csg[i];
        bb += csb[i + 2 * r + 1] - csb[i]; ba += csa[i + 2 * r + 1] - csa[i];
      }
    }
  }

  // Fill out[0..n) with box averages for a span row, fast path when it fits.
  static inline void blur_row(image_t *target, int x, int y, int n, int r, int W, int H, uint32_t *out) {
    if(r <= BLUR_MAX_R) blur_row_averages(target, x, y, n, r, W, H, out);
    else for(int i = 0; i < n; i++) out[i] = box_average(target, x + i, y, r, W, H);
  }

  // Per-span sampler, shared by the solid and masked batches: mask == nullptr is
  // the solid path, mask != nullptr lerps the blur into dst by per-pixel coverage.
  static void blur_span(image_t *target, blur_brush_t *p, int x, int y, int w, const uint8_t *mask);

  blur_brush_t::blur_brush_t(int radius) : radius(radius) {}

  void blur_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    blur_brush_t *p = this;
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) blur_span(target, p, spans[i].x, spans[i].y, spans[i].w, nullptr);
  }

  void blur_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step)
      blur_span(target, this, spans[i].x, spans[i].y, spans[i].w, (const uint8_t*)spans[i].mask);
  }

  // ── helpers ─────────────────────────────────────────────────────────────────

  static void blur_span(image_t *target, blur_brush_t *p, int x, int y, int w, const uint8_t *mask) {
    int r = p->radius < 1 ? 1 : p->radius;
    rect_t b = target->bounds();
    int W = (int)b.w, H = (int)b.h;

    uint32_t tmp[BLUR_CHUNK];
    while(w > 0) {
      int n = w < BLUR_CHUNK ? w : BLUR_CHUNK;
      bool covered = true;
      if(mask) {
        covered = false;
        for(int i = 0; i < n && !covered; i++) covered = mask[i] != 0;
      }
      if(covered) {
        blur_row(target, x, y, n, r, W, H, tmp);
        uint32_t *dst = (uint32_t*)target->ptr(x, y);
        if(mask) {
          for(int i = 0; i < n; i++) if(mask[i]) dst[i] = blur_mask_lerp(dst[i], tmp[i], mask[i]);
        } else {
          for(int i = 0; i < n; i++) dst[i] = tmp[i];
        }
      }
      x += n;
      w -= n;
      if(mask) mask += n;
    }
  }

}
