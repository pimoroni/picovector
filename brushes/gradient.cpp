#include <cmath>

#include "../brush.hpp"

namespace picovector {

  // recover straight (non-premultiplied) channels from a premultiplied packed colour
  static inline void unpremultiply(uint32_t packed, float &r, float &g, float &b, float &a) {
    uint32_t pa = _a(packed);
    if(pa == 0) { r = g = b = a = 0.0f; return; }
    float inv = 255.0f / (float)pa;
    r = (float)_r(packed) * inv;
    g = (float)_g(packed) * inv;
    b = (float)_b(packed) * inv;
    a = (float)pa;
  }

  // pack straight channels back into the premultiplied layout color_t::premul uses
  static inline uint32_t premultiply_pack(float r, float g, float b, float a) {
    int ai = (int)(a + 0.5f);
    if(ai < 0) ai = 0; else if(ai > 255) ai = 255;
    int rp = (int)(r * ai / 255.0f + 0.5f); if(rp < 0) rp = 0; else if(rp > 255) rp = 255;
    int gp = (int)(g * ai / 255.0f + 0.5f); if(gp < 0) gp = 0; else if(gp > 255) gp = 255;
    int bp = (int)(b * ai / 255.0f + 0.5f); if(bp < 0) bp = 0; else if(bp > 255) bp = 255;
    return __builtin_bswap32((rp << 24) | (gp << 16) | (bp << 8) | ai);
  }

  // Pre-render the gradient into the 256-entry LUT: interpolate stops in straight
  // sRGB (the SVG default), then store premultiplied. Spread method is pad.
  static void build_lut(uint32_t *lut, const float *positions, const uint32_t *premul_colors, int n) {
    if(n <= 0) {
      for(int i = 0; i < 256; i++) lut[i] = 0;
      return;
    }

    // sanitise stop offsets: clamp to 0..1 and force non-decreasing (per SVG)
    float pos[gradient_brush_t::max_stops];
    float sr[gradient_brush_t::max_stops], sg[gradient_brush_t::max_stops];
    float sb[gradient_brush_t::max_stops], sa[gradient_brush_t::max_stops];
    float last = 0.0f;
    for(int i = 0; i < n; i++) {
      float pp = positions[i];
      if(pp < 0.0f) pp = 0.0f; else if(pp > 1.0f) pp = 1.0f;
      if(pp < last) pp = last;
      last = pp;
      pos[i] = pp;
      unpremultiply(premul_colors[i], sr[i], sg[i], sb[i], sa[i]);
    }

    int j = 0; // current segment; advances monotonically as t increases
    for(int i = 0; i < 256; i++) {
      float t = (float)i / 255.0f;
      float r, g, b, a;

      if(t <= pos[0]) {
        r = sr[0]; g = sg[0]; b = sb[0]; a = sa[0];
      } else if(t >= pos[n - 1]) {
        r = sr[n - 1]; g = sg[n - 1]; b = sb[n - 1]; a = sa[n - 1];
      } else {
        while(j < n - 2 && pos[j + 1] < t) j++;
        float span = pos[j + 1] - pos[j];
        float f = span > 0.0f ? (t - pos[j]) / span : 0.0f;
        r = sr[j] + (sr[j + 1] - sr[j]) * f;
        g = sg[j] + (sg[j + 1] - sg[j]) * f;
        b = sb[j] + (sb[j + 1] - sb[j]) * f;
        a = sa[j] + (sa[j + 1] - sa[j]) * f;
      }

      lut[i] = premultiply_pack(r, g, b, a);
    }
  }

  gradient_brush_t::gradient_brush_t(gradient_type_t type, float x1, float y1, float x2, float y2,
                                     const float *positions, const uint32_t *premul_colors, int stop_count,
                                     mat3_t *transform)
    : type(type), p1(x1, y1), p2(x2, y2) {
    if(transform) {
      inverse_transform = *transform;
      inverse_transform.inverse();
    }
    build_lut(lut, positions, premul_colors, stop_count);
  }

  // --- linear ---------------------------------------------------------------

  void gradient_brush_linear_span_func(image_t *target, brush_t *brush, int x, int y, int w) {
    gradient_brush_t *p = (gradient_brush_t*)brush;
    uint32_t *dst = (uint32_t*)target->ptr(x, y);
    blend_func_t fn = target->_blend_func;
    const uint32_t *lut = p->lut;

    // pixel -> gradient space, plus the per-pixel (+1 in screen x) step
    vec2_t pt = vec2_t((float)x, (float)y).transform(&p->inverse_transform);
    float dpx = p->inverse_transform.v00;
    float dpy = p->inverse_transform.v10;

    float dx = p->p2.x - p->p1.x;
    float dy = p->p2.y - p->p1.y;
    float inv_len2 = 1.0f / (dx * dx + dy * dy + 1e-12f);

    // offset along the gradient axis is linear across the span, so step it
    float t  = ((pt.x - p->p1.x) * dx + (pt.y - p->p1.y) * dy) * inv_len2;
    float dt = (dpx * dx + dpy * dy) * inv_len2;

    while(w--) {
      int idx = (int)(t * 255.0f + 0.5f);
      if(idx < 0) idx = 0; else if(idx > 255) idx = 255;
      uint32_t c = lut[idx];
      *dst = fn(*dst, _r(c), _g(c), _b(c), _a(c));
      dst++;
      t += dt;
    }
  }

  void gradient_brush_linear_masked_span_func(image_t *target, brush_t *brush, int x, int y, int w, uint8_t *mask) {
    gradient_brush_t *p = (gradient_brush_t*)brush;
    uint32_t *dst = (uint32_t*)target->ptr(x, y);
    blend_func_t fn = target->_blend_func;
    const uint32_t *lut = p->lut;

    vec2_t pt = vec2_t((float)x, (float)y).transform(&p->inverse_transform);
    float dpx = p->inverse_transform.v00;
    float dpy = p->inverse_transform.v10;

    float dx = p->p2.x - p->p1.x;
    float dy = p->p2.y - p->p1.y;
    float inv_len2 = 1.0f / (dx * dx + dy * dy + 1e-12f);

    float t  = ((pt.x - p->p1.x) * dx + (pt.y - p->p1.y) * dy) * inv_len2;
    float dt = (dpx * dx + dpy * dy) * inv_len2;

    while(w--) {
      int idx = (int)(t * 255.0f + 0.5f);
      if(idx < 0) idx = 0; else if(idx > 255) idx = 255;
      uint32_t c = lut[idx];
      uint32_t m = *mask;
      *dst = fn(*dst, (_r(c) * m + 128) >> 8, (_g(c) * m + 128) >> 8,
                      (_b(c) * m + 128) >> 8, (_a(c) * m + 128) >> 8);
      dst++;
      mask++;
      t += dt;
    }
  }

  // --- radial ---------------------------------------------------------------

  void gradient_brush_radial_span_func(image_t *target, brush_t *brush, int x, int y, int w) {
    gradient_brush_t *p = (gradient_brush_t*)brush;
    uint32_t *dst = (uint32_t*)target->ptr(x, y);
    blend_func_t fn = target->_blend_func;
    const uint32_t *lut = p->lut;

    vec2_t pt = vec2_t((float)x, (float)y).transform(&p->inverse_transform);
    float dpx = p->inverse_transform.v00;
    float dpy = p->inverse_transform.v10;

    float rx = p->p2.x - p->p1.x;
    float ry = p->p2.y - p->p1.y;
    float radius = sqrtf(rx * rx + ry * ry);
    float inv_r = radius > 0.0f ? 1.0f / radius : 0.0f;

    float px = pt.x, py = pt.y;
    while(w--) {
      float ex = px - p->p1.x;
      float ey = py - p->p1.y;
      float t = sqrtf(ex * ex + ey * ey) * inv_r;
      int idx = (int)(t * 255.0f + 0.5f);
      if(idx < 0) idx = 0; else if(idx > 255) idx = 255;
      uint32_t c = lut[idx];
      *dst = fn(*dst, _r(c), _g(c), _b(c), _a(c));
      dst++;
      px += dpx;
      py += dpy;
    }
  }

  void gradient_brush_radial_masked_span_func(image_t *target, brush_t *brush, int x, int y, int w, uint8_t *mask) {
    gradient_brush_t *p = (gradient_brush_t*)brush;
    uint32_t *dst = (uint32_t*)target->ptr(x, y);
    blend_func_t fn = target->_blend_func;
    const uint32_t *lut = p->lut;

    vec2_t pt = vec2_t((float)x, (float)y).transform(&p->inverse_transform);
    float dpx = p->inverse_transform.v00;
    float dpy = p->inverse_transform.v10;

    float rx = p->p2.x - p->p1.x;
    float ry = p->p2.y - p->p1.y;
    float radius = sqrtf(rx * rx + ry * ry);
    float inv_r = radius > 0.0f ? 1.0f / radius : 0.0f;

    float px = pt.x, py = pt.y;
    while(w--) {
      float ex = px - p->p1.x;
      float ey = py - p->p1.y;
      float t = sqrtf(ex * ex + ey * ey) * inv_r;
      int idx = (int)(t * 255.0f + 0.5f);
      if(idx < 0) idx = 0; else if(idx > 255) idx = 255;
      uint32_t c = lut[idx];
      uint32_t m = *mask;
      *dst = fn(*dst, (_r(c) * m + 128) >> 8, (_g(c) * m + 128) >> 8,
                      (_b(c) * m + 128) >> 8, (_a(c) * m + 128) >> 8);
      dst++;
      mask++;
      px += dpx;
      py += dpy;
    }
  }

  span_func_t gradient_brush_t::span_func() {
    return type == GRADIENT_RADIAL ? gradient_brush_radial_span_func
                                   : gradient_brush_linear_span_func;
  }

  masked_span_func_t gradient_brush_t::masked_span_func() {
    return type == GRADIENT_RADIAL ? gradient_brush_radial_masked_span_func
                                   : gradient_brush_linear_masked_span_func;
  }

}
