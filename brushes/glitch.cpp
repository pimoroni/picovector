#include "../brush.hpp"

namespace picovector {

  // See color.cpp for the brush-file layout. Animated datamosh glitch: horizontal
  // bands slide sideways, active bands get a magenta/cyan chromatic split, and
  // occasional pure magenta/cyan lines flash across the frame. Keyed on PV_TICKS
  // (captured at construction) so it animates; `amount` (0..255) scales how many
  // bands glitch, the slide distance and the line frequency. Reads along the row,
  // so seams every CH pixels may double-glitch - on brand for the effect.

  static inline uint32_t h32(uint32_t a) {
    a ^= a >> 16; a *= 0x7feb352du; a ^= a >> 15; a *= 0x846ca68bu; a ^= a >> 16; return a;
  }

  #define CH 96

  glitch_brush_t::glitch_brush_t(int amount) : amount(amount), t((uint32_t)PV_TICKS) {}

  // per-row glitch parameters
  static inline void row_params(uint32_t t, int amount, int y,
                                bool *line, uint32_t *line_col, int *shift, int *ca) {
    uint32_t tf = t / 80;                                       // ~12 steps/sec
    uint32_t hl = h32((uint32_t)y * 2246822519u ^ tf * 3266489917u);
    if((int)(hl & 0x3ff) < (amount >> 1)) {                     // rare bright line
      *line = true;
      *line_col = (hl & 0x400)
        ? (uint32_t)(0   | (255 << 8) | (255 << 16) | (255u << 24))   // cyan
        : (uint32_t)(255 | (0   << 8) | (255 << 16) | (255u << 24));  // magenta
      return;
    }
    *line = false; *shift = 0; *ca = 0;
    uint32_t hb = h32((uint32_t)(y >> 3) * 2654435761u ^ tf * 40503u);   // 8px bands
    if((int)(hb & 0xff) < amount) {                            // band active
      *shift = ((int)((hb >> 8) & 0x7f) - 64) * amount / 512;  // ~ +/- amount/8 px
      *ca = 1 + (int)((hb >> 16) & 3);                         // 1..4 px split
    }
  }

  void glitch_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    rect_t bnd = target->bounds(); int W = (int)bnd.w;
    const pv_span *spans = _spans();
    uint32_t tmp[CH];
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      bool line; uint32_t line_col = 0; int shift = 0, ca = 0;
      row_params(t, amount, y, &line, &line_col, &shift, &ca);
      if(line) {
        int lr = line_col & 0xff, lg = (line_col >> 8) & 0xff, lb = (line_col >> 16) & 0xff;
        uint8_t *p = (uint8_t*)target->ptr(x, y);
        for(int k = 0; k < w; k++) {   // 50% opaque line over the content
          p[0] = (uint8_t)(p[0] + (((lr - p[0]) * 128) >> 8));
          p[1] = (uint8_t)(p[1] + (((lg - p[1]) * 128) >> 8));
          p[2] = (uint8_t)(p[2] + (((lb - p[2]) * 128) >> 8));
          p += 4;
        }
        continue;
      }
      while(w > 0) {
        int n = w < CH ? w : CH;
        for(int k = 0; k < n; k++) {
          int sx = x + k + shift;
          int xr = sx - ca, xg = sx, xb = sx + ca;
          if(xr < 0) xr = 0; else if(xr >= W) xr = W - 1;
          if(xg < 0) xg = 0; else if(xg >= W) xg = W - 1;
          if(xb < 0) xb = 0; else if(xb >= W) xb = W - 1;
          uint8_t *pr = (uint8_t*)target->ptr(xr, y);
          uint8_t *pg = (uint8_t*)target->ptr(xg, y);
          uint8_t *pb = (uint8_t*)target->ptr(xb, y);
          tmp[k] = (uint32_t)(pr[0] | (pg[1] << 8) | (pb[2] << 16) | (pg[3] << 24));
        }
        uint8_t *p = (uint8_t*)target->ptr(x, y);
        for(int k = 0; k < n; k++) { *(uint32_t*)p = tmp[k]; p += 4; }
        x += n; w -= n;
      }
    }
  }

  void glitch_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    rect_t bnd = target->bounds(); int W = (int)bnd.w;
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y;
      const uint8_t *mask = spans[i].mask;
      bool line; uint32_t line_col = 0; int shift = 0, ca = 0;
      row_params(t, amount, y, &line, &line_col, &shift, &ca);
      uint8_t *p = (uint8_t*)target->ptr(x, y);
      int lr = line_col & 0xff, lg = (line_col >> 8) & 0xff, lb = (line_col >> 16) & 0xff;
      // ease each channel toward the glitched value by coverage so AA edges feather in
      for(int w = spans[i].w; w; w--) {
        int m = *mask++, nr, ng, nb;
        if(!m) { p += 4; x++; continue; }
        if(line) {   // 50% opaque line, then feathered by coverage
          nr = p[0] + (((lr - p[0]) * 128) >> 8);
          ng = p[1] + (((lg - p[1]) * 128) >> 8);
          nb = p[2] + (((lb - p[2]) * 128) >> 8);
        } else {
          int sx = x + shift, xr = sx - ca, xb = sx + ca;
          if(xr < 0) xr = 0; else if(xr >= W) xr = W - 1;
          if(xb < 0) xb = 0; else if(xb >= W) xb = W - 1;
          int xg = sx < 0 ? 0 : (sx >= W ? W - 1 : sx);
          nr = ((uint8_t*)target->ptr(xr, y))[0];
          ng = ((uint8_t*)target->ptr(xg, y))[1];
          nb = ((uint8_t*)target->ptr(xb, y))[2];
        }
        p[0] = (uint8_t)(p[0] + (((nr - p[0]) * m) >> 8));
        p[1] = (uint8_t)(p[1] + (((ng - p[1]) * m) >> 8));
        p[2] = (uint8_t)(p[2] + (((nb - p[2]) * m) >> 8));
        p += 4; x++;
      }
    }
  }

}
