#include <cmath>

#include "../brush.hpp"

namespace picovector {

  // recover straight (non-premultiplied) channels from a premultiplied packed colour
  static inline void unpremultiply(pixel_t packed, float &r, float &g, float &b, float &a) {
    uint32_t pa = _a(packed);
    if(pa == 0) { r = g = b = a = 0.0f; return; }
    float inv = 255.0f / (float)pa;
    r = (float)_r(packed) * inv;
    g = (float)_g(packed) * inv;
    b = (float)_b(packed) * inv;
    a = (float)pa;
  }

  // pack straight channels back into the premultiplied layout color_t::premul uses
  static inline pixel_t premultiply_pack(float r, float g, float b, float a) {
    int ai = (int)(a + 0.5f);
    if(ai < 0) ai = 0; else if(ai > 255) ai = 255;
    int rp = (int)(r * ai / 255.0f + 0.5f); if(rp < 0) rp = 0; else if(rp > 255) rp = 255;
    int gp = (int)(g * ai / 255.0f + 0.5f); if(gp < 0) gp = 0; else if(gp > 255) gp = 255;
    int bp = (int)(b * ai / 255.0f + 0.5f); if(bp < 0) bp = 0; else if(bp > 255) bp = 255;
    return __builtin_bswap32((rp << 24) | (gp << 16) | (bp << 8) | ai);
  }

  // Pre-render the gradient into the 256-entry LUT: interpolate stops in straight
  // sRGB (the SVG default), then store premultiplied. Spread method is pad.
  static void build_lut(pixel_t *lut, const float *positions, const pixel_t *premul_colors, int n) {
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

  // Per-span samplers, shared by the solid and masked batches: mask == nullptr is
  // the solid path, mask != nullptr folds per-pixel coverage into the colour.
  static void gradient_linear_span(image_t *target, gradient_brush_t *p, int x, int y, int w, const uint8_t *mask);
  static void gradient_radial_span(image_t *target, gradient_brush_t *p, int x, int y, int w, const uint8_t *mask);
  static void gradient_conical_span(image_t *target, gradient_brush_t *p, int x, int y, int w, const uint8_t *mask);

  gradient_brush_t::gradient_brush_t(gradient_type_t type, float x1, float y1, float x2, float y2,
                                     const float *positions, const pixel_t *premul_colors, int stop_count,
                                     mat3_t *transform)
    // Store a known type rather than relying on every consumer's fallthrough:
    // the binding casts an int straight from Python.
    : type(type > GRADIENT_CONICAL ? GRADIENT_LINEAR : type) {
    geometry(x1, y1, x2, y2, transform);
    build_lut(lut, positions, premul_colors, stop_count);
  }

  void gradient_brush_t::geometry(float x1, float y1, float x2, float y2, mat3_t *transform) {
    p1 = vec2_t(x1, y1);
    p2 = vec2_t(x2, y2);

    // The conical sweep's zero angle. Coincident points leave it pointing along
    // +x, which is as good as any other answer for a gradient with no direction.
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if(len > 1e-9f) { dir_c = dx / len; dir_s = dy / len; }
    else            { dir_c = 1.0f;     dir_s = 0.0f; }

    if(transform) {
      base_inverse = *transform;
      base_inverse.inverse();
    } else {
      base_inverse = mat3_t();
    }

    inverse_transform = base_inverse; // no shape transform applied yet
  }

  // Fold the shape's transform into the gradient so it moves/scales/rotates with
  // the shape: device->gradient = (brush's own inverse) * inverse(shape transform).
  void gradient_brush_t::set_render_transform(mat3_t *transform) {
    if(!transform) { inverse_transform = base_inverse; return; }
    mat3_t inv = *transform;
    inv.inverse();
    inverse_transform = base_inverse;
    inverse_transform.multiply(inv); // base_inverse * inverse(shape)
  }

  // Batch: dispatch on gradient type once, then run the shared per-span sampler.
  void gradient_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    gradient_brush_t *p = this;
    const pv_span *spans = _spans();
    switch(p->type) {
      case GRADIENT_RADIAL:
        for(int i = i0; i < i1; i += step) gradient_radial_span(target, p, spans[i].x, spans[i].y, spans[i].w, nullptr);
        break;
      case GRADIENT_CONICAL:
        for(int i = i0; i < i1; i += step) gradient_conical_span(target, p, spans[i].x, spans[i].y, spans[i].w, nullptr);
        break;
      default:
        for(int i = i0; i < i1; i += step) gradient_linear_span(target, p, spans[i].x, spans[i].y, spans[i].w, nullptr);
        break;
    }
  }

  void gradient_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    gradient_brush_t *p = this;
    const pv_masked_span *spans = _masked_spans();
    switch(p->type) {
      case GRADIENT_RADIAL:
        for(int i = i0; i < i1; i += step)
          gradient_radial_span(target, p, spans[i].x, spans[i].y, spans[i].w, (const uint8_t*)spans[i].mask);
        break;
      case GRADIENT_CONICAL:
        for(int i = i0; i < i1; i += step)
          gradient_conical_span(target, p, spans[i].x, spans[i].y, spans[i].w, (const uint8_t*)spans[i].mask);
        break;
      default:
        for(int i = i0; i < i1; i += step)
          gradient_linear_span(target, p, spans[i].x, spans[i].y, spans[i].w, (const uint8_t*)spans[i].mask);
        break;
    }
  }

  // ── helpers ─────────────────────────────────────────────────────────────────

  // --- linear ---------------------------------------------------------------

  static void gradient_linear_span(image_t *target, gradient_brush_t *p, int x, int y, int w, const uint8_t *mask) {
    uint32_t *dst = (uint32_t*)target->ptr(x, y);
    const uint32_t *lut = p->lut;

    // pixel -> gradient space, plus the per-pixel step for a one-pixel screen step
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
      uint32_t m = mask ? *mask++ : 255u;
      if(m) {
        int idx = (int)(t * 255.0f + 0.5f);
        if(idx < 0) idx = 0; else if(idx > 255) idx = 255;
        blend_masked_over_premul(dst, lut[idx], m);
      }
      dst++;
      t += dt;
    }
  }

  // --- radial ---------------------------------------------------------------

  static void gradient_radial_span(image_t *target, gradient_brush_t *p, int x, int y, int w, const uint8_t *mask) {
    uint32_t *dst = (uint32_t*)target->ptr(x, y);
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
      uint32_t m = mask ? *mask++ : 255u;
      if(m) {
        float ex = px - p->p1.x;
        float ey = py - p->p1.y;
        float t = sqrtf(ex * ex + ey * ey) * inv_r;
        int idx = (int)(t * 255.0f + 0.5f);
        if(idx < 0) idx = 0; else if(idx > 255) idx = 255;
        blend_masked_over_premul(dst, lut[idx], m);
      }
      dst++;
      px += dpx;
      py += dpy;
    }
  }

  // --- conical --------------------------------------------------------------

  // atan(i / 128) in Q12 turns (4096 == a full turn), so entries run 0..512, an
  // eighth of a turn. The largest step between entries is 6 counts (0.53
  // degrees), well inside the colour table's own 1.41 degrees per entry, so the
  // approximation is not what limits a sweep's smoothness.
  static const uint16_t atan_q12[129] = {
       0,    5,   10,   15,   20,   25,   31,   36,
      41,   46,   51,   56,   61,   66,   71,   76,
      81,   86,   91,   96,  101,  106,  111,  116,
     121,  126,  131,  136,  140,  145,  150,  155,
     160,  164,  169,  174,  179,  183,  188,  193,
     197,  202,  207,  211,  216,  220,  225,  229,
     234,  238,  243,  247,  252,  256,  260,  265,
     269,  273,  277,  282,  286,  290,  294,  298,
     302,  306,  310,  314,  318,  322,  326,  330,
     334,  338,  342,  346,  349,  353,  357,  360,
     364,  368,  371,  375,  379,  382,  386,  389,
     393,  396,  399,  403,  406,  410,  413,  416,
     419,  423,  426,  429,  432,  435,  439,  442,
     445,  448,  451,  454,  457,  460,  463,  466,
     469,  471,  474,  477,  480,  483,  486,  488,
     491,  494,  496,  499,  502,  504,  507,  509,
     512,
  };

  // The angle around p1, measured clockwise from the p1->p2 direction.
  //
  // Unlike linear's offset, the angle cannot be stepped: its derivative goes as
  // 1/r^2, so the same screen step turns through 1.6x more angle at the inner
  // edge of a 16px annulus than at the outer. What *can* be stepped is the
  // offset vector itself, which is affine in x, leaving one divide per pixel as
  // the irreducible part.
  //
  // That divide and the table lookup stay in integers. Keeping the whole pixel
  // loop off the FPU is worth more than the cheaper divide: it also removes the
  // float->int convert, the register move and its hazard, and the two-sided
  // clamp that the other two samplers pay on every pixel.
  static void gradient_conical_span(image_t *target, gradient_brush_t *p, int x, int y, int w, const uint8_t *mask) {
    uint32_t *dst = (uint32_t*)target->ptr(x, y);
    const uint32_t *lut = p->lut;

    vec2_t pt = vec2_t((float)x, (float)y).transform(&p->inverse_transform);
    float dpx = p->inverse_transform.v00;
    float dpy = p->inverse_transform.v10;

    // Rotate the offset from p1 into the sweep's frame, so the start direction
    // becomes +u and the angle is measured from there.
    float c = p->dir_c, s = p->dir_s;
    float ex = pt.x - p->p1.x, ey = pt.y - p->p1.y;
    float u = ex * c + ey * s,   v = -ex * s + ey * c;
    float du = dpx * c + dpy * s, dv = -dpx * s + dpy * c;

    // Only the ratio of u to v matters, so any scale will do. Pick one from this
    // span's own extent, which keeps the fixed-point values large enough to hold
    // precision and small enough that the << 7 below cannot overflow, whatever
    // the transform does to the coordinate scale.
    float ue = u + du * (float)(w - 1), ve = v + dv * (float)(w - 1);
    float mag = fabsf(u);
    if(fabsf(v)  > mag) mag = fabsf(v);
    if(fabsf(ue) > mag) mag = fabsf(ue);
    if(fabsf(ve) > mag) mag = fabsf(ve);
    float k = mag > 1e-20f ? 1048576.0f / mag : 0.0f;   // scale the span into +/-2^20

    int32_t uq = (int32_t)(u * k),  vq = (int32_t)(v * k);
    int32_t duq = (int32_t)(du * k), dvq = (int32_t)(dv * k);

    while(w--) {
      uint32_t m = mask ? *mask++ : 255u;
      if(m) {
        int32_t au = uq < 0 ? -uq : uq;
        int32_t av = vq < 0 ? -vq : vq;
        int32_t hi = au >= av ? au : av;
        int32_t lo = au >= av ? av : au;

        // 0..128, rounded. hi == 0 only at the exact centre, where the angle is
        // undefined and any answer will do.
        uint32_t z = hi ? (uint32_t)(((uint32_t)lo << 7) + (uint32_t)(hi >> 1)) / (uint32_t)hi : 0u;

        uint32_t aq = atan_q12[z];
        if(au < av) aq = 1024u - aq;      // fold about the 45 degree diagonal
        if(uq < 0)  aq = 2048u - aq;      // and about the quarter turn
        if(vq < 0)  aq = 4096u - aq;      // and about the half turn

        // Q12 turns to a table index. The wrap is free and it is the right
        // answer here: the domain is a circle, so index 256 is index 0.
        blend_masked_over_premul(dst, lut[((aq + 8u) >> 4) & 255u], m);
      }
      dst++;
      uq += duq;
      vq += dvq;
    }
  }

}
