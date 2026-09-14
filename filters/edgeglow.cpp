#include <cstdint>
#include <cstring>
#include <cstdlib>

#include "../picovector.hpp"
#include "../image.hpp"
#include "../util.hpp"

namespace picovector {

  // Glowing edges (like Photoshop's Glowing Edges): a Sobel edge magnitude drives
  // brightness, the edges keep the source colour (saturation-boosted) on a black
  // field, then bloom makes them glow. `strength` scales edge sensitivity. Reads a
  // 3x3 neighbourhood, so the original is copied into a scratch image first and
  // read from there (no in-place or cross-core corruption).

  static inline uint8_t clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v); }

  // One column's two Sobel combinations, plus the centre luminance the colouring
  // step needs. V is the vertically weighted sum that gx differences across
  // columns; D the bottom-minus-top that gy weights across columns.
  struct edge_col_t { int c, v, d, m; };

  static inline void edge_column(edge_col_t &k, int c, const uint8_t *rt,
                                 const uint8_t *rm, const uint8_t *rb) {
    int t = luminance(rt + (size_t)c * 4);
    int b = luminance(rb + (size_t)c * 4);
    k.c = c;
    k.m = luminance(rm + (size_t)c * 4);
    k.v = t + 2 * k.m + b;
    k.d = b - t;
  }

  void image_t::edgeglow(int strength) {
    // An indexed image is one byte a pixel; this writes four. See brush.cpp.
    if(_has_palette) return;
    rect_t bd = bounds(); int W = (int)bd.w, H = (int)bd.h;
    if(W < 3 || H < 3) return;
    image_t *src = make_scratch_image(W, H);
    if(!src) return;
    for(int y = 0; y < H; y++)
      memcpy(src->ptr(0, y), ptr(0, y), (size_t)W * 4);

    int fx0, fy0, fx1, fy1;
    if(!filter_rect(fx0, fy0, fx1, fy1)) { free_scratch_image(src); return; }

    for(int y = fy0; y < fy1; y++) {
      int y0 = y > 0 ? y - 1 : 0, y1 = y < H - 1 ? y + 1 : H - 1;
      const uint8_t *rt = (const uint8_t*)src->ptr(0, y0);
      const uint8_t *rm = (const uint8_t*)src->ptr(0, y);
      const uint8_t *rb = (const uint8_t*)src->ptr(0, y1);
      uint8_t *out = (uint8_t*)ptr(fx0, y);

      // Sobel reads three columns a pixel, and consecutive pixels share two of
      // them. Carrying the columns along the row takes the nine luminance taps
      // a pixel down to three, which is the whole cost of this loop.
      edge_col_t col[3];
      edge_column(col[0], fx0 > 0 ? fx0 - 1 : 0, rt, rm, rb);
      edge_column(col[1], fx0, rt, rm, rb);
      edge_column(col[2], fx0 < W - 1 ? fx0 + 1 : W - 1, rt, rm, rb);

      for(int x = fx0; x < fx1; x++) {
        if(x > fx0) {
          int right = x < W - 1 ? x + 1 : W - 1;
          col[0] = col[1];
          col[1] = col[2];
          // at the right edge the clamp leaves the carried column already right
          if(col[2].c != right) edge_column(col[2], right, rt, rm, rb);
        }
        int gx = col[2].v - col[0].v;
        int gy = col[0].d + 2 * col[1].d + col[2].d;
        int mag = ((abs(gx) + abs(gy)) * strength) >> 8;
        if(mag > 255) mag = 255;
        // edges glow in the source colour (saturation-boosted); flat areas go black
        const uint8_t *s = rm + (size_t)x * 4;
        int l = col[1].m;
        int r = clamp8(l + (s[0] - l) * 2), g = clamp8(l + (s[1] - l) * 2), b = clamp8(l + (s[2] - l) * 2);
        out[0] = (uint8_t)(r * mag / 255);
        out[1] = (uint8_t)(g * mag / 255);
        out[2] = (uint8_t)(b * mag / 255); // leave alpha
        out += 4;
      }
    }
    free_scratch_image(src);
    bloom(40, 260, 4);   // glow the bright edges
  }

}
