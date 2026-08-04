#include <cmath>

#include "../brush.hpp"

namespace picovector {

  // Fractal (fBm) value noise through a colour ramp: clouds, smoke, fire, terrain,
  // marbling. See color.cpp for the brush-file layout.
  //
  // Each octave interpolates a grid of hashed per-cell values. The hash is a
  // doubly-indexed permutation table and the interpolation fade is quintic, so the
  // field is isotropic and stays C2 continuous across cell boundaries.
  //
  // Ramp positions are area fractions of the field: a stop at 0.6 sits where 60% of
  // the field is below it. The constructor measures the distribution once and
  // ramp() maps through it.

  static void fractal_span(image_t *target, fractal_brush_t *p, int x, int y, int w, const uint8_t *mask);
  static void init_fade();
  static void shuffle(uint8_t *perm, uint32_t seed);
  static inline int fractal_at(const fractal_brush_t *p, int32_t x, int32_t y);
  static inline int value_at(const uint8_t *perm, int32_t x, int32_t y, int wrap);

  // Black to white, so a brush built without a ramp shows the field itself.
  static const float default_positions[2] = { 0.0f, 1.0f };
  static const color_t default_stops[2] = { rgb_color_t(0, 0, 0, 255),
                                            rgb_color_t(255, 255, 255, 255) };

  // Shared by every instance; the corner permutation is per-brush, so that a
  // seed picks an independent field rather than a different part of one.
  static uint8_t fade_lut[256];
  static bool fade_ready = false;

  fractal_brush_t::fractal_brush_t(float scale, int octaves, float persistence,
                                   int repeat, uint32_t seed, mat3_t *transform)
    : octaves(octaves < 1 ? 1 : (octaves > max_octaves ? max_octaves : octaves)),
      seed(seed) {
    init_fade();
    shuffle(perm, seed);

    // The finest octave's grid is 2^(octaves-1) times denser than the coarsest, and
    // its cell index has to stay inside the 256-entry permutation table, which is
    // what caps the period.
    int limit = 256 >> (this->octaves - 1);
    int wanted = (repeat > 0 && repeat < limit) ? repeat : limit;
    int period = 1;
    while(period * 2 <= wanted) period *= 2;
    this->repeat = period;
    for(int o = 0; o < this->octaves; o++) wrap[o] = (period << o) - 1;

    if(persistence < 0.05f) persistence = 0.05f;
    else if(persistence > 0.95f) persistence = 0.95f;

    // Each octave contributes `persistence` of the one below it. Weights total 256
    // so the fractal sum normalises with a shift; largest remainder keeps the total
    // exact however they round.
    float share[max_octaves], sum = 0.0f, next = 1.0f;
    for(int o = 0; o < this->octaves; o++) {
      share[o] = next;
      sum += next;
      next *= persistence;
    }
    int total = 0;
    for(int o = 0; o < this->octaves; o++) {
      share[o] = share[o] * 256.0f / sum;
      weight[o] = (int)share[o];
      total += weight[o];
    }
    while(total < 256) {
      int best = 0;
      float largest = -2.0f;
      for(int o = 0; o < this->octaves; o++) {
        float frac = share[o] - (float)weight[o];
        if(frac > largest) { largest = frac; best = o; }
      }
      weight[best]++;
      share[best] -= 1.0f;  // below every rival's fraction, so it is not picked twice
      total++;
    }

    cell = scale > 0.01f ? scale : 0.01f;
    geometry(transform);

    measure();
    ramp(default_positions, default_stops, 2);
  }

  void fractal_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step)
      fractal_span(target, this, spans[i].x, spans[i].y, spans[i].w, nullptr);
  }

  void fractal_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step)
      fractal_span(target, this, spans[i].x, spans[i].y, spans[i].w, (const uint8_t*)spans[i].mask);
  }

  // Fold the shape's transform into the field so it moves/scales/rotates with the
  // shape: device->field = (brush's own inverse) * inverse(shape transform).
  void fractal_brush_t::set_render_transform(mat3_t *transform) {
    if(!transform) { inverse_transform = base_inverse; return; }
    mat3_t inv = *transform;
    inv.inverse();
    inverse_transform = base_inverse;
    inverse_transform.multiply(inv);
  }

  void fractal_brush_t::geometry(mat3_t *transform) {
    placement = transform ? *transform : mat3_t();
    rebuild();
  }

  // Below about a pixel per cell the field is sampled faster than it varies, but a
  // placement can scale further either way, so only a singular size is refused.
  void fractal_brush_t::resize(float scale) {
    cell = scale > 0.01f ? scale : 0.01f;
    rebuild();
  }

  // The cell size is intrinsic and the placement sits outside it, so a placement
  // that only rotates or translates leaves feature size alone.
  void fractal_brush_t::rebuild() {
    base_inverse = placement;
    base_inverse.scale(cell);
    base_inverse.inverse();     // a singular transform is left alone, not NaN'd
    inverse_transform = base_inverse;
  }

  // For each noise value, the fraction of the field below it as a 0..255 index.
  // Depends only on the octave weights, so it runs once per brush.
  void fractal_brush_t::measure() {
    // Not a simple fraction of a cell, so interiors are sampled as well as corners.
    static const int grid = 96;
    static const int32_t stride = 30011;  // ~0.458 cells, Q16

    uint16_t bins[256];
    for(int i = 0; i < 256; i++) bins[i] = 0;
    for(int j = 0; j < grid; j++) {
      int32_t y = (int32_t)j * stride;
      for(int i = 0; i < grid; i++) bins[fractal_at(this, (int32_t)i * stride, y)]++;
    }

    const float samples = (float)(grid * grid);
    int below = 0;
    for(int v = 0; v < 256; v++) {
      // Mid-bin, so equal-area steps come out evenly spaced along the ramp.
      float mid = (float)below + (float)bins[v] * 0.5f;
      int at = (int)(mid * 255.0f / samples + 0.5f);
      if(at < 0) at = 0; else if(at > 255) at = 255;
      equalised[v] = (uint8_t)at;
      below += bins[v];
    }
  }

  void fractal_brush_t::ramp(const float *positions, const color_t *stops, int stop_count) {
    if(stop_count > max_stops) stop_count = max_stops;

    // The table is indexed by noise value, so each stop moves from the area
    // fraction it was given to the value sitting at that fraction of the field.
    // equalised[] is non-decreasing, so a scan finds it and stop order survives.
    float mapped[max_stops];
    for(int i = 0; i < stop_count; i++) {
      float p = positions[i];
      if(p < 0.0f) p = 0.0f; else if(p > 1.0f) p = 1.0f;
      int at = (int)(p * 255.0f + 0.5f);
      int v = 0;
      while(v < 255 && equalised[v] < at) v++;
      mapped[i] = (float)v / 255.0f;
    }

    sample_ramp(lut, 256, mapped, stops, stop_count);
  }

  // ── helpers ─────────────────────────────────────────────────────────────────

  // Fisher-Yates from the seed. The seed is avalanched first, so seeds 1 and 2 give
  // unrelated permutations rather than ones sharing a long prefix.
  static void shuffle(uint8_t *perm, uint32_t seed) {
    for(int i = 0; i < 256; i++) perm[i] = (uint8_t)i;

    uint32_t state = seed;
    state ^= state >> 16; state *= 2246822519u;
    state ^= state >> 13; state *= 3266489917u;
    state ^= state >> 16; state |= 1u;

    for(int i = 255; i > 0; i--) {
      state = state * 1103515245u + 12345u;
      int j = (int)((state >> 16) % (uint32_t)(i + 1));
      uint8_t swap = perm[i]; perm[i] = perm[j]; perm[j] = swap;
    }
  }

  static void init_fade() {
    if(fade_ready) return;
    for(int i = 0; i < 256; i++) {
      float t = (float)i / 256.0f;
      fade_lut[i] = (uint8_t)(255.0f * t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f) + 0.5f);
    }

    fade_ready = true;
  }

  // One octave at a Q16 position in cells. Arithmetic shifts floor, so a negative
  // position wraps the same way a positive one does.
  static inline int value_at(const uint8_t *perm, int32_t x, int32_t y, int wrap) {
    int nx0 = (int)((x >> 16) & wrap), ny0 = (int)((y >> 16) & wrap);
    int ny1 = (ny0 + 1) & wrap;
    int h0 = perm[nx0], h1 = perm[(nx0 + 1) & wrap];
    int v00 = perm[(h0 + ny0) & 255], v01 = perm[(h0 + ny1) & 255];
    int v10 = perm[(h1 + ny0) & 255], v11 = perm[(h1 + ny1) & 255];
    int fx = fade_lut[(x >> 8) & 255], fy = fade_lut[(y >> 8) & 255];
    int left  = v00 + (((v01 - v00) * fy) >> 8);
    int right = v10 + (((v11 - v10) * fy) >> 8);
    return left + (((right - left) * fx) >> 8);
  }

  // The whole sum, for measuring. Each octave doubles the frequency. The span loop
  // computes the same thing with the corner lookups cached, so the two must agree.
  static inline int fractal_at(const fractal_brush_t *p, int32_t x, int32_t y) {
    int acc = 0;
    for(int o = 0; o < p->octaves; o++)
      acc += p->weight[o] * value_at(p->perm, x << o, y << o, p->wrap[o]);
    return acc >> 8;
  }

  // Wrapped into the octave's own period, so the field tiles and a whole span's
  // worth of stepping stays inside an int32.
  static inline int32_t wrap_q16(float cells, int period) {
    float wrapped = fmodf(cells, (float)period);
    if(wrapped < 0.0f) wrapped += (float)period;
    return (int32_t)(wrapped * 65536.0f);
  }

  static void fractal_span(image_t *target, fractal_brush_t *p, int x, int y, int w, const uint8_t *mask) {
    uint32_t *dst = (uint32_t*)target->ptr(x, y);
    const pixel_t *lut = p->lut;
    const uint8_t *perm = p->perm;
    const int octaves = p->octaves;

    // pixel -> field space (cells), plus the per-pixel step for a one-pixel screen
    // step. The transform carries the cell size, so no scaling is left to do here.
    vec2_t pt = vec2_t((float)x, (float)y).transform(&p->inverse_transform);
    float dpx = p->inverse_transform.v00;
    float dpy = p->inverse_transform.v10;

    int32_t pos_x[fractal_brush_t::max_octaves], pos_y[fractal_brush_t::max_octaves];
    int32_t step_x[fractal_brush_t::max_octaves], step_y[fractal_brush_t::max_octaves];
    int cell[fractal_brush_t::max_octaves];
    int c00[fractal_brush_t::max_octaves], c01[fractal_brush_t::max_octaves];
    int c10[fractal_brush_t::max_octaves], c11[fractal_brush_t::max_octaves];

    for(int o = 0; o < octaves; o++) {
      float freq = (float)(1 << o);
      pos_x[o] = wrap_q16(pt.x * freq, p->wrap[o] + 1);
      pos_y[o] = wrap_q16(pt.y * freq, p->wrap[o] + 1);
      step_x[o] = (int32_t)(dpx * freq * 65536.0f);
      step_y[o] = (int32_t)(dpy * freq * 65536.0f);
      cell[o] = -1;
      c00[o] = c01[o] = c10[o] = c11[o] = 0;
    }

    while(w--) {
      uint32_t m = mask ? *mask++ : 255u;
      if(m) {
        int acc = 0;
        for(int o = 0; o < octaves; o++) {
          int32_t px = pos_x[o], py = pos_y[o];
          int mask = p->wrap[o];
          int nx0 = (int)((px >> 16) & mask), ny0 = (int)((py >> 16) & mask);

          // Corner hashes only change on a cell boundary.
          int key = nx0 | (ny0 << 8);
          if(key != cell[o]) {
            cell[o] = key;
            int ny1 = (ny0 + 1) & mask;
            int h0 = perm[nx0], h1 = perm[(nx0 + 1) & mask];
            c00[o] = perm[(h0 + ny0) & 255]; c01[o] = perm[(h0 + ny1) & 255];
            c10[o] = perm[(h1 + ny0) & 255]; c11[o] = perm[(h1 + ny1) & 255];
          }

          int fx = fade_lut[(px >> 8) & 255], fy = fade_lut[(py >> 8) & 255];
          int left  = c00[o] + (((c01[o] - c00[o]) * fy) >> 8);
          int right = c10[o] + (((c11[o] - c10[o]) * fy) >> 8);
          acc += p->weight[o] * (left + (((right - left) * fx) >> 8));
        }

        // Composited, not stored, so transparent stops overlay existing content.
        pixel_t src = lut[acc >> 8];
        *dst = blend_over_premul(*dst, m == 255u ? src : _premul_mul_alpha(src, m));
      }
      dst++;
      for(int o = 0; o < octaves; o++) { pos_x[o] += step_x[o]; pos_y[o] += step_y[o]; }
    }
  }

}
