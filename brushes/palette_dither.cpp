#include <cmath>
#include "../brush.hpp"

namespace picovector {

  // See color.cpp for the brush-file layout.

  static inline uint8_t clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v); }
  static int nearest_index(const uint32_t *pal, int n, int r, int g, int b);

  // 8x8 Bayer threshold matrix, 0..63
  static const uint8_t bayer8[64] = {
     0, 32,  8, 40,  2, 34, 10, 42,
    48, 16, 56, 24, 50, 18, 58, 26,
    12, 44,  4, 36, 14, 46,  6, 38,
    60, 28, 52, 20, 62, 30, 54, 22,
     3, 35, 11, 43,  1, 33,  9, 41,
    51, 19, 59, 27, 49, 17, 57, 25,
    15, 47,  7, 39, 13, 45,  5, 37,
    63, 31, 55, 23, 61, 29, 53, 21,
  };

  palette_dither_brush_t::palette_dither_brush_t(const uint32_t *colors, int count, int strength) {
    n = count < 1 ? 1 : (count > 64 ? 64 : count);
    for(int i = 0; i < n; i++) pal[i] = colors[i];

    // spread = mean nearest-neighbour distance, scaled by strength (128 = 1.0)
    int acc = 0;
    if(n > 1) {
      for(int i = 0; i < n; i++) {
        long best = -1;
        for(int j = 0; j < n; j++) {
          if(j == i) continue;
          int dr = (int)(pal[i] & 0xff)        - (int)(pal[j] & 0xff);
          int dg = (int)((pal[i] >> 8) & 0xff)  - (int)((pal[j] >> 8) & 0xff);
          int db = (int)((pal[i] >> 16) & 0xff) - (int)((pal[j] >> 16) & 0xff);
          long d = (long)dr * dr + (long)dg * dg + (long)db * db;
          if(best < 0 || d < best) best = d;
        }
        acc += (int)sqrtf((float)best);
      }
      acc /= n;
    }
    spread = acc * strength / 128;

    // 12-bit rgb cube -> nearest palette index, sampled at each cell centre
    cube = (uint8_t *)PV_MALLOC_NO_SCAN(4096);
    for(int r = 0; r < 16; r++)
      for(int g = 0; g < 16; g++)
        for(int b = 0; b < 16; b++)
          cube[(r << 8) | (g << 4) | b] = (uint8_t)nearest_index(pal, n, (r << 4) | 8, (g << 4) | 8, (b << 4) | 8);
  }

  palette_dither_brush_t::~palette_dither_brush_t() { 
#if MICROPY_MALLOC_USES_ALLOCATED_SIZE
    PV_FREE(cube, 4096);
#else
    PV_FREE(cube);
#endif
  }

  void palette_dither_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y;
      uint8_t *p = (uint8_t*)target->ptr(x, y);
      const uint8_t *row = bayer8 + (y & 7) * 8;
      for(int w = spans[i].w; w; w--) {
        int bias = ((row[x & 7] - 32) * spread) >> 6;
        int r = clamp8(p[0] + bias), g = clamp8(p[1] + bias), b = clamp8(p[2] + bias);
        *(uint32_t*)p = pal[cube[((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)]];
        p += 4; x++;
      }
    }
  }

  void palette_dither_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y;
      uint8_t *p = (uint8_t*)target->ptr(x, y);
      const uint8_t *mask = spans[i].mask;
      const uint8_t *row = bayer8 + (y & 7) * 8;
      // ease each channel toward the palette colour by coverage so AA edges feather in
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        int bias = ((row[x & 7] - 32) * spread) >> 6;
        int r = clamp8(p[0] + bias), g = clamp8(p[1] + bias), b = clamp8(p[2] + bias);
        uint32_t t = pal[cube[((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)]];
        int nr = t & 0xff, ng = (t >> 8) & 0xff, nb = (t >> 16) & 0xff;
        p[0] = (uint8_t)(p[0] + (((nr - p[0]) * m) >> 8));
        p[1] = (uint8_t)(p[1] + (((ng - p[1]) * m) >> 8));
        p[2] = (uint8_t)(p[2] + (((nb - p[2]) * m) >> 8));
        p += 4; x++;
      }
    }
  }

  // ── helpers ─────────────────────────────────────────────────────────────────
  // nearest palette entry by squared RGB distance
  static int nearest_index(const uint32_t *pal, int n, int r, int g, int b) {
    int best = 0; long bd = 0x7fffffff;
    for(int i = 0; i < n; i++) {
      int dr = r - (int)(pal[i] & 0xff), dg = g - (int)((pal[i] >> 8) & 0xff), db = b - (int)((pal[i] >> 16) & 0xff);
      long d = (long)dr * dr + (long)dg * dg + (long)db * db;
      if(d < bd) { bd = d; best = i; }
    }
    return best;
  }

}
