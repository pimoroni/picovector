#include "../brush.hpp"

namespace picovector {

  // See color.cpp for the brush-file layout. Painterly Kuwahara-style filter:
  // each covered pixel is replaced by the average colour of the fullest of 8
  // luminance buckets over its (2r+1)^2 neighbourhood, then eased back toward the
  // original by `strength` (0..255). At radius >= 3 the neighbourhood is sampled
  // every other pixel to keep the cost down. It reads the target neighbourhood,
  // so - like blur.cpp - a span is computed into a stack temp before being
  // written back, otherwise freshly-written pixels would corrupt reads still to
  // come along the row.

  oilpaint_brush_t::oilpaint_brush_t(int radius, int strength)
    : radius(radius < 1 ? 1 : (radius > 4 ? 4 : radius)),
      strength(strength < 0 ? 0 : (strength > 255 ? 255 : strength)),
      sstep(radius >= 3 ? 2 : 1) {}

  #define CH 64

  static inline uint32_t oil_at(image_t *target, int x, int y, int radius, int sstep, int W, int H) {
    int cnt[8] = {0}; long sr[8] = {0}, sg[8] = {0}, sb[8] = {0};
    for(int yy = y - radius; yy <= y + radius; yy += sstep) {
      int cy = yy < 0 ? 0 : (yy >= H ? H - 1 : yy);
      for(int xx = x - radius; xx <= x + radius; xx += sstep) {
        int cxx = xx < 0 ? 0 : (xx >= W ? W - 1 : xx);
        uint8_t *q = (uint8_t*)target->ptr(cxx, cy);
        int lum = luminance(q); int bkt = lum >> 5; // 0..7
        cnt[bkt]++; sr[bkt] += q[0]; sg[bkt] += q[1]; sb[bkt] += q[2];
      }
    }
    int bb = 0; for(int j = 1; j < 8; j++) if(cnt[j] > cnt[bb]) bb = j;
    int r = (int)(sr[bb] / cnt[bb]), g = (int)(sg[bb] / cnt[bb]), b = (int)(sb[bb] / cnt[bb]);
    return (uint32_t)(r | (g << 8) | (b << 16) | (255 << 24));
  }

  void oilpaint_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    rect_t bnd = target->bounds(); int W = (int)bnd.w, H = (int)bnd.h;
    const pv_span *spans = _spans();
    uint32_t tmp[CH];
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      while(w > 0) {
        int n = w < CH ? w : CH;
        for(int k = 0; k < n; k++) {
          uint32_t o = oil_at(target, x + k, y, radius, sstep, W, H);
          uint8_t *c = (uint8_t*)target->ptr(x + k, y);          // original centre
          int r = c[0] + (((int)(o & 0xff)        - c[0]) * strength >> 8);
          int g = c[1] + (((int)((o >> 8) & 0xff)  - c[1]) * strength >> 8);
          int b = c[2] + (((int)((o >> 16) & 0xff) - c[2]) * strength >> 8);
          tmp[k] = (uint32_t)(r | (g << 8) | (b << 16) | (c[3] << 24));
        }
        uint8_t *p = (uint8_t*)target->ptr(x, y);
        for(int k = 0; k < n; k++) { *(uint32_t*)p = tmp[k]; p += 4; }
        x += n; w -= n;
      }
    }
  }

  void oilpaint_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    rect_t bnd = target->bounds(); int W = (int)bnd.w, H = (int)bnd.h;
    const pv_masked_span *spans = _masked_spans();
    uint32_t tmp[CH];
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      const uint8_t *mask = spans[i].mask;
      while(w > 0) {
        int n = w < CH ? w : CH;
        for(int k = 0; k < n; k++) if(mask[k]) tmp[k] = oil_at(target, x + k, y, radius, sstep, W, H);
        uint8_t *p = (uint8_t*)target->ptr(x, y);
        // ease toward the (strength-scaled) dominant colour by coverage so AA edges feather in
        for(int k = 0; k < n; k++) {
          int m = *mask++;
          if(!m) { p += 4; continue; }
          int pr = (int)(tmp[k] & 0xff), pg = (int)((tmp[k] >> 8) & 0xff), pb = (int)((tmp[k] >> 16) & 0xff);
          int nr = p[0] + ((pr - p[0]) * strength >> 8);
          int ng = p[1] + ((pg - p[1]) * strength >> 8);
          int nb = p[2] + ((pb - p[2]) * strength >> 8);
          p[0] = (uint8_t)(p[0] + ((nr - p[0]) * m >> 8));
          p[1] = (uint8_t)(p[1] + ((ng - p[1]) * m >> 8));
          p[2] = (uint8_t)(p[2] + ((nb - p[2]) * m >> 8));
          p += 4; // alpha kept
        }
        x += n; w -= n;
      }
    }
  }

}
