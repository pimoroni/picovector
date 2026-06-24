// pico3d triangle rasteriser.
//
// Per pixel there is NO barycentric: the screen-affine attributes (NDC depth,
// 1/w, u/w, v/w, vertex colour) are interpolated by linearly stepping them
// along each scanline (attrs_t), with a single perspective divide per textured
// pixel. This is the main throughput win on the FPU-equipped M33.
//
// Coverage (the inside test) has two interchangeable backends sharing the same
// integer edge functions:
//   * scalar reference  — tracks all three edge functions with int adds; what
//     the host unit tests exercise.
//   * INTERP0 path       — RP2350 hardware: lanes hold e0,e1 and result-lane-2
//     yields -e2, giving all three weights + the inside test in one accumulator
//     step. Enabled with -DPICO3D_USE_INTERP on device; scalar on host. The two
//     produce identical coverage, so the scalar host tests cover both.

#include "pico3d.hpp"

#include <algorithm>
#include <cmath>

#if defined(PICO3D_USE_INTERP) || defined(PICO3D_USE_INTERP_BARY)
#include "hardware/interp.h"
#endif

namespace picovector {

  using std::min;
  using std::max;

  // Triangles whose screen bbox is at most this many pixels on a side use affine
  // (not perspective-correct) UV — the foreshortening error is sub-pixel at that
  // size, and it skips the per-pixel divide + a setup plane. Tunable.
  static constexpr int PICO3D_AFFINE_MAX = 24;

  // ---- helpers -------------------------------------------------------------

  // repeat-wrap a texel coordinate (handles negatives) — these models use
  // tiled UVs that run outside [0,1]. Power-of-two dimensions take a mask fast-path
  // (1 cycle) instead of the integer SDIV `%` (~7 cycles); the branch is on a
  // constant texture dimension so it predicts perfectly. NO power-of-two
  // requirement — non-PoT sizes fall back to `%`.
  static inline int tex_wrap(int a, int n) {
    if ((n & (n - 1)) == 0) return a & (n - 1);   // PoT: mask (two's-complement handles a<0)
    a %= n; return a < 0 ? a + n : a;
  }

  static inline uint32_t tex_nearest(const pico3d_texture_t *t, float u, float v) {
    // covered pixels have u,v in [0,1] (convex combo of vertex UVs), so u*w >= 0
    // and an (int) truncate equals floorf exactly — but skips the libm floorf call.
    int x = tex_wrap((int)(u * t->width), t->width);
    int y = tex_wrap((int)(v * t->height), t->height);
    return t->texels[y * t->width + x];
  }

  static inline uint32_t lerp_rgb(uint32_t a, uint32_t b, float t) {
    int ar = a & 0xff, ag = (a >> 8) & 0xff, ab = (a >> 16) & 0xff, aa = (a >> 24) & 0xff;
    int br = b & 0xff, bg = (b >> 8) & 0xff, bb = (b >> 16) & 0xff, ba = (b >> 24) & 0xff;
    int r = ar + (int)((br - ar) * t);
    int g = ag + (int)((bg - ag) * t);
    int bl = ab + (int)((bb - ab) * t);
    int al = aa + (int)((ba - aa) * t);                    // blend alpha (else cutoff kills it)
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)bl << 16) | ((uint32_t)al << 24);
  }

  static inline uint32_t tex_bilinear(const pico3d_texture_t *t, float u, float v) {
    float fx = u * t->width - 0.5f, fy = v * t->height - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float tx = fx - x0, ty = fy - y0;
    int x1 = x0 + 1, y1 = y0 + 1;
    x0 = tex_wrap(x0, t->width);  x1 = tex_wrap(x1, t->width);
    y0 = tex_wrap(y0, t->height); y1 = tex_wrap(y1, t->height);
    uint32_t a = lerp_rgb(t->texels[y0 * t->width + x0], t->texels[y0 * t->width + x1], tx);
    uint32_t b = lerp_rgb(t->texels[y1 * t->width + x0], t->texels[y1 * t->width + x1], tx);
    return lerp_rgb(a, b, ty);
  }

  // Screen-affine per-pixel attributes, stepped along each scanline so there is
  // NO per-pixel barycentric. z is NDC depth; iw/uw/vw are 1/w, u/w, v/w; r/g/b
  // are the (affine) vertex colour; nrm/tan are the world normal/tangent for
  // normal mapping. Each group is only stepped when needed (uvs / varying /
  // nmap), so the common paths pay nothing.
  struct attrs_t {
    bool  uvs, varying, persp, nmap;
    float z, iw, uw, vw, r, g, b;                          // running (current pixel)
    float dzdx, diwdx, duwdx, dvwdx, drdx, dgdx, dbdx;     // per-pixel (x) step
    float z_row, iw_row, uw_row, vw_row, r_row, g_row, b_row;
    float dzdy, diwdy, duwdy, dvwdy, drdy, dgdy, dbdy;     // per-row (y) step
    float nrm[3], nrm_dx[3], nrm_dy[3], nrm_row[3];        // world normal (nmap)
    float tan[3], tan_dx[3], tan_dy[3], tan_row[3];        // world tangent (nmap)

    void row_reset() {
      z = z_row;
      if (uvs)     { uw = uw_row; vw = vw_row; if (persp) iw = iw_row; }
      if (varying) { r = r_row; g = g_row; b = b_row; }
      if (nmap)    for (int k = 0; k < 3; k++) { nrm[k] = nrm_row[k]; tan[k] = tan_row[k]; }
    }
    void row_advance() {
      z_row += dzdy;
      if (uvs)     { uw_row += duwdy; vw_row += dvwdy; if (persp) iw_row += diwdy; }
      if (varying) { r_row += drdy; g_row += dgdy; b_row += dbdy; }
      if (nmap)    for (int k = 0; k < 3; k++) { nrm_row[k] += nrm_dy[k]; tan_row[k] += tan_dy[k]; }
    }
    void px_advance() {
      z += dzdx;
      if (uvs)     { uw += duwdx; vw += dvwdx; if (persp) iw += diwdx; }
      if (varying) { r += drdx; g += dgdx; b += dbdx; }
      if (nmap)    for (int k = 0; k < 3; k++) { nrm[k] += nrm_dx[k]; tan[k] += tan_dx[k]; }
    }
    uint32_t base(uint32_t cc) const {
      if (!varying) return cc;
      int R = (int)(r + 0.5f), G = (int)(g + 0.5f), B = (int)(b + 0.5f);
      R = R < 0 ? 0 : (R > 255 ? 255 : R);
      G = G < 0 ? 0 : (G > 255 ? 255 : G);
      B = B < 0 ? 0 : (B > 255 ? 255 : B);
      return (uint32_t)R | ((uint32_t)G << 8) | ((uint32_t)B << 16);
    }
  };

  // Per-triangle shading context (built once before the pixel loop).
  struct shade_t {
    pico3d_target_t        *t;
    const pico3d_material_t *m;
    const pico3d_light_t   *light;
    const pico3d_texture_t *tx;      // texture, or the matcap sphere-map (sampled as a texture)
    vec3_t L;                       // -light->direction (world), normalised; nmap only
    bool tex, persp, nmap;
  };

  // Write one covered pixel from already-interpolated attributes. The tex and
  // nmap blocks are loop-invariant predicted branches, so a triangle with
  // neither runs exactly the old depth-test + write.
  static inline bool emit(const shade_t &s, int x, int y, const attrs_t &at, uint32_t base) {
    float dn = at.z * 0.5f + 0.5f;
    dn = dn < 0.0f ? 0.0f : (dn > 1.0f ? 1.0f : dn);
    uint16_t d16 = (uint16_t)(dn * 65535.0f);
    int didx = y * s.t->depth_stride + x;
    if (s.t->depth && d16 >= s.t->depth[didx]) return false;   // not nearer

    uint32_t col = base;

    float u = 0.0f, v = 0.0f;
    if (s.tex || s.nmap) {
      if (s.persp) { float rw = 1.0f / at.iw; u = at.uw * rw; v = at.vw * rw; }
      else         { u = at.uw; v = at.vw; }
    }
    if (s.tex) {   // s.tx is the texture, or the matcap sphere-map (UV baked per vertex)
      uint32_t px = (s.m->filter == PICO3D_BILINEAR) ? tex_bilinear(s.tx, u, v)
                                                     : tex_nearest(s.tx, u, v);
      if (((px >> 24) & 0xffu) < s.m->alpha_cutoff) return false;   // alpha cutout
      col = pico3d_modulate(col, px);
    }
    if (s.nmap) {
      // Smooth base: interpolated world normal AND tangent (not renormalised per
      // pixel — they stay ~unit across a triangle). Only the final world normal
      // is normalised, via the proven vec3 helper.
      vec3_t N(at.nrm[0], at.nrm[1], at.nrm[2]);
      vec3_t T(at.tan[0], at.tan[1], at.tan[2]);
      vec3_t B = N.cross(T);
      uint32_t np = (s.m->filter == PICO3D_BILINEAR) ? tex_bilinear(s.m->normal_map, u, v)
                                                     : tex_nearest(s.m->normal_map, u, v);
      float mx = (float)(np        & 0xff) * (1.0f / 127.5f) - 1.0f;   // R -> x
      float my = (float)((np >> 8)  & 0xff) * (1.0f / 127.5f) - 1.0f;  // G -> y
      float mz = (float)((np >> 16) & 0xff) * (1.0f / 127.5f) - 1.0f;  // B -> z
      vec3_t wn = (T * mx + B * my + N * mz).normalized();
      col = pico3d_modulate(col, pico3d_light_value(s.light, wn.dot(s.L)));
    }
    s.t->color[y * s.t->color_stride + x] = col;
    if (s.t->depth) s.t->depth[didx] = d16;
    return true;
  }

  // ---- entry point ---------------------------------------------------------

  void pico3d_depth_clear(pico3d_target_t *t, uint16_t value) {
    if (!t->depth) return;
    for (int y = 0; y < t->height; y++) {
      uint16_t *row = t->depth + y * t->depth_stride;
      for (int x = 0; x < t->width; x++) row[x] = value;
    }
  }

  int pico3d_raster_triangle(pico3d_target_t *t, const pico3d_tri_t *tri,
                             const pico3d_material_t *m, const pico3d_light_t *light) {
    // viewport map: clip -> ndc -> screen; keep per-vertex z(ndc), 1/w, u/w, v/w,
    // raw u,v (for affine small-tri texturing), and colour.
    float sx[3], sy[3], z[3], iw[3], uw[3], vw[3], uv0[3], uv1[3];
    uint32_t rgb[3];
    for (int i = 0; i < 3; i++) {
      sx[i] = tri->sx[i]; sy[i] = tri->sy[i];   // cached screen position (no divide here)
      z[i]  = tri->z[i];  iw[i] = tri->iw[i];
      uv0[i] = tri->uv_[i].x;            // raw u
      uv1[i] = tri->uv_[i].y;            // raw v
      uw[i] = uv0[i] * iw[i];           // u/w
      vw[i] = uv1[i] * iw[i];           // v/w
      rgb[i] = tri->rgb[i];
    }

    int X0 = (int)lroundf(sx[0]), Y0 = (int)lroundf(sy[0]);
    int X1 = (int)lroundf(sx[1]), Y1 = (int)lroundf(sy[1]);
    int X2 = (int)lroundf(sx[2]), Y2 = (int)lroundf(sy[2]);

    int32_t denom = (X1 - X0) * (Y2 - Y0) - (Y1 - Y0) * (X2 - X0);   // integer (interp paths)
    // Sub-pixel signed area (float) drives the cull, attribute planes and the scalar
    // coverage, so vertices keep their fractional screen position (no integer snap /
    // PS1 wobble). The integer `denom`/edge setup below is only for the interp paths.
    float denomf = (sx[1]-sx[0]) * (sy[2]-sy[0]) - (sy[1]-sy[0]) * (sx[2]-sx[0]);
    if (denomf == 0.0f) return 0;                    // degenerate
    // Front face (CCW in clip space) has negative screen-space area after the
    // viewport Y-flip. Keep front faces; cull back faces unless double-sided.
    int sign;
    if (denomf < 0.0f) { sign = -1; }
    else { if (!m->double_sided) return 0; sign = 1; }

    // bounding box from the float extents (sub-pixel), clamped to the clip rect
    float minxf = min(sx[0], min(sx[1], sx[2])), maxxf = max(sx[0], max(sx[1], sx[2]));
    float minyf = min(sy[0], min(sy[1], sy[2])), maxyf = max(sy[0], max(sy[1], sy[2]));
    int minx = max(t->clip_x0, (int)floorf(minxf));
    int maxx = min(t->clip_x1 - 1, (int)floorf(maxxf) + 1);   // floorf+1 (avoid pulling ceilf)
    int miny = max(t->clip_y0, (int)floorf(minyf));
    int maxy = min(t->clip_y1 - 1, (int)floorf(maxyf) + 1);
    if (minx > maxx || miny > maxy) return 0;

    // --- coverage: integer edge functions, e_i >= 0 inside -------------------
    int32_t A0 = (Y1 - Y2), B0 = (X2 - X1);
    int32_t A1 = (Y2 - Y0), B1 = (X0 - X2);
    int32_t A2 = (Y0 - Y1), B2 = (X1 - X0);
    int32_t e0_row = (X2 - X1) * (miny - Y1) - (Y2 - Y1) * (minx - X1);
    int32_t e1_row = (X0 - X2) * (miny - Y2) - (Y0 - Y2) * (minx - X2);
    int32_t e2_row = (X1 - X0) * (miny - Y0) - (Y1 - Y0) * (minx - X0);
    if (sign < 0) {
      A0 = -A0; B0 = -B0; e0_row = -e0_row;
      A1 = -A1; B1 = -B1; e1_row = -e1_row;
      A2 = -A2; B2 = -B2; e2_row = -e2_row;
    }

    // --- attribute planes (screen-affine; uses the geometric, un-flipped denom)
    float invD = 1.0f / denomf;
    float dx1 = sx[1] - sx[0], dy1 = sy[1] - sy[0];     // float (sub-pixel) vertex deltas
    float dx2 = sx[2] - sx[0], dy2 = sy[2] - sy[0];
    float px0 = (float)minx + 0.5f, py0 = (float)miny + 0.5f;   // first pixel CENTRE
    auto plane = [&](float a0, float a1, float a2, float &ddx, float &ddy, float &row) {
      float d1 = a1 - a0, d2 = a2 - a0;
      ddx = (d1 * dy2 - d2 * dy1) * invD;
      ddy = (d2 * dx1 - d1 * dx2) * invD;
      row = a0 + ddx * (px0 - sx[0]) + ddy * (py0 - sy[0]);   // value at (minx+.5, miny+.5)
    };

    attrs_t at{};
    // Normal mapping needs a map, a light, and per-vertex normals+tangents (the
    // draw stage only fills tri->n/tan when all three are present).
    bool nmap = (m->normal_map != nullptr) && (light != nullptr);
    // Matcap is a sphere-map texture: its reflection UV is baked per vertex in the
    // draw stage, so it's just a textured triangle sampling m->matcap (no per-pixel
    // normal). normal_map wins if both are set.
    bool matcap = (m->matcap != nullptr) && !nmap;
    const pico3d_texture_t *tx = m->texture ? m->texture : (matcap ? m->matcap : nullptr);
    bool has_tex = (tx != nullptr);
    at.nmap = nmap;
    at.varying = !(rgb[0] == rgb[1] && rgb[1] == rgb[2]);
    at.uvs = has_tex || nmap;   // need u,v for texture/matcap and/or normal-map sampling
    // Small triangles barely foreshorten, so interpolate u,v affinely (2 planes,
    // no per-pixel divide). Large ones keep perspective-correct u/w,v/w,1/w.
    at.persp = at.uvs && ((maxx - minx) > PICO3D_AFFINE_MAX || (maxy - miny) > PICO3D_AFFINE_MAX);
    uint32_t cc = rgb[0];
    plane(z[0], z[1], z[2], at.dzdx, at.dzdy, at.z_row);
    if (at.uvs) {
      if (at.persp) {
        plane(iw[0], iw[1], iw[2], at.diwdx, at.diwdy, at.iw_row);
        plane(uw[0], uw[1], uw[2], at.duwdx, at.duwdy, at.uw_row);   // u/w
        plane(vw[0], vw[1], vw[2], at.dvwdx, at.dvwdy, at.vw_row);   // v/w
      } else {
        // affine: interpolate u,v directly (uw/vw slots hold u,v)
        plane(uv0[0], uv0[1], uv0[2], at.duwdx, at.duwdy, at.uw_row);
        plane(uv1[0], uv1[1], uv1[2], at.dvwdx, at.dvwdy, at.vw_row);
      }
    }
    if (at.varying) {
      plane((float)(rgb[0] & 0xff), (float)(rgb[1] & 0xff), (float)(rgb[2] & 0xff),
            at.drdx, at.drdy, at.r_row);
      plane((float)((rgb[0] >> 8) & 0xff), (float)((rgb[1] >> 8) & 0xff), (float)((rgb[2] >> 8) & 0xff),
            at.dgdx, at.dgdy, at.g_row);
      plane((float)((rgb[0] >> 16) & 0xff), (float)((rgb[1] >> 16) & 0xff), (float)((rgb[2] >> 16) & 0xff),
            at.dbdx, at.dbdy, at.b_row);
    }

    if (nmap) {
      // interpolate the per-vertex world normal + tangent for the per-pixel TBN
      plane(tri->n[0].x, tri->n[1].x, tri->n[2].x, at.nrm_dx[0], at.nrm_dy[0], at.nrm_row[0]);
      plane(tri->n[0].y, tri->n[1].y, tri->n[2].y, at.nrm_dx[1], at.nrm_dy[1], at.nrm_row[1]);
      plane(tri->n[0].z, tri->n[1].z, tri->n[2].z, at.nrm_dx[2], at.nrm_dy[2], at.nrm_row[2]);
      plane(tri->tan[0].x, tri->tan[1].x, tri->tan[2].x, at.tan_dx[0], at.tan_dy[0], at.tan_row[0]);
      plane(tri->tan[0].y, tri->tan[1].y, tri->tan[2].y, at.tan_dx[1], at.tan_dy[1], at.tan_row[1]);
      plane(tri->tan[0].z, tri->tan[1].z, tri->tan[2].z, at.tan_dx[2], at.tan_dy[2], at.tan_row[2]);
    }

    shade_t s{};
    s.t = t; s.m = m; s.light = light; s.tx = tx;
    s.tex = has_tex; s.persp = at.persp; s.nmap = nmap;
    if (nmap) s.L = (-light->direction).normalized();

    int written = 0;

#if defined(PICO3D_USE_INTERP_BARY)
    // Normalized-barycentric coverage (the OVERF trick). ACCUM1 = a = e1/|denom|,
    // ACCUM0 = b = e0/|denom| as 16.16; the mask passes only the fraction [15:0],
    // so a weight leaving [0,1) sets masked-off MSBs -> the hardware OVERF flag
    // (one bit covering "a or b out of range"). base2 = -1.0 makes
    // result2 = a + b - 1 = -c, so its sign is the third edge:
    //   inside = !OVERF && result2 <= 0.
    // Stepping is in software by default (writes the full 32-bit accumulator each
    // pixel — robust). PICO3D_BARY_AUTOSTEP instead auto-steps via add_raw, which
    // is the elegant form BUT add_raw is 24-bit and (per the datasheet) carries
    // into the masked-off bits, so a negative-direction step false-trips OVERF;
    // that variant is the on-hardware experiment, not the verified path.
    interp_config cfg = interp_default_config();
    interp_config_set_shift(&cfg, 0);
    interp_config_set_mask(&cfg, 0, 15);
    interp_config_set_signed(&cfg, false);
    interp_set_config(interp0, 0, &cfg);
    interp_set_config(interp0, 1, &cfg);
    interp0->base[2] = 0xFFFF0000u;                 // -1.0 in 16.16

    // normalize to 16.16 in float (FPU) — avoids pulling in a 64-bit-division
    // libgcc helper, keeping this path's codegen profile like the edge path.
    int32_t adenom = denom * sign;                  // |denom| > 0
    float inv = 65536.0f / (float)adenom;
    int32_t a_dx  = (int32_t)((float)A1 * inv);
    int32_t b_dx  = (int32_t)((float)A0 * inv);
    int32_t a_dy  = (int32_t)((float)B1 * inv);
    int32_t b_dy  = (int32_t)((float)B0 * inv);
    int32_t a_row = (int32_t)((float)e1_row * inv);
    int32_t b_row = (int32_t)((float)e0_row * inv);

    for (int y = miny; y <= maxy; y++) {
      at.row_reset();
  #ifdef PICO3D_BARY_AUTOSTEP
      interp0->accum[1] = (uint32_t)a_row;          // seed; hardware steps within the row
      interp0->accum[0] = (uint32_t)b_row;
  #else
      int32_t a = a_row, b = b_row;
  #endif
      for (int x = minx; x <= maxx; x++) {
  #ifndef PICO3D_BARY_AUTOSTEP
        interp0->accum[1] = (uint32_t)a;            // software step: full 32-bit write/pixel
        interp0->accum[0] = (uint32_t)b;
  #endif
        bool inside = !(interp0->ctrl[0] & (1u << 25))      // OVERF: a or b left [0,1)
                      && (int32_t)interp0->peek[2] <= 0;    // result2 = -c <= 0  =>  c >= 0
        if (inside) {
          if (emit(s, x, y, at, at.base(cc))) written++;
        }
  #ifdef PICO3D_BARY_AUTOSTEP
        interp0->add_raw[1] = (uint32_t)a_dx;       // 24-bit hardware add (see note)
        interp0->add_raw[0] = (uint32_t)b_dx;
  #else
        a += a_dx; b += b_dx;
  #endif
        at.px_advance();
      }
      a_row += a_dy; b_row += b_dy;
      at.row_advance();
    }
#elif defined(PICO3D_USE_INTERP)
    // RP2350 INTERP0 does coverage: lane0=e0, lane1=e1, result2 = base2+e0+e1.
    // With base2 = -|denom|, peek[2] = -e2, so one accumulator step gives all
    // three edge weights + the inside test. Run the accumulators as 24-bit
    // SIGNED (mask 0..23) because the add_raw auto-increment register is 24-bit
    // (0x00ffffff); edge magnitudes are well under 2^23 so signed steps wrap
    // correctly. (Attributes are stepped separately in float, below.)
    interp_config cfg = interp_default_config();
    interp_config_set_shift(&cfg, 0);
    interp_config_set_mask(&cfg, 0, 23);
    interp_config_set_signed(&cfg, true);
    interp_set_config(interp0, 0, &cfg);
    interp_set_config(interp0, 1, &cfg);
    interp0->base[2] = (uint32_t)(-(denom * sign));   // -|denom|

    for (int y = miny; y <= maxy; y++) {
      interp0->accum[0] = (uint32_t)e0_row;
      interp0->accum[1] = (uint32_t)e1_row;
      at.row_reset();
      for (int x = minx; x <= maxx; x++) {
        int32_t e0 = (int32_t)interp0->peek[0];
        int32_t e1 = (int32_t)interp0->peek[1];
        int32_t e2 = -(int32_t)interp0->peek[2];
        if (((e0 | e1 | e2)) >= 0) {
          if (emit(s, x, y, at, at.base(cc))) written++;
        }
        interp0->add_raw[0] = (uint32_t)A0;
        interp0->add_raw[1] = (uint32_t)A1;
        at.px_advance();
      }
      e0_row += B0; e1_row += B1;
      at.row_advance();
    }
#else
    // scalar coverage: SUB-PIXEL float edge functions sampled at pixel CENTRES, so
    // vertices keep their fractional position (no integer snap → no wobble/swim).
    // Adjacent triangles share identical float vertices, so a shared edge function
    // is the exact negation between them → watertight (no cracks).
    float fA0 = (sy[1]-sy[2]), fB0 = (sx[2]-sx[1]);   // d/dx, d/dy of edge 0 (V1->V2)
    float fA1 = (sy[2]-sy[0]), fB1 = (sx[0]-sx[2]);   // edge 1 (V2->V0)
    float fA2 = (sy[0]-sy[1]), fB2 = (sx[1]-sx[0]);   // edge 2 (V0->V1)
    float cpx = (float)minx + 0.5f, cpy = (float)miny + 0.5f;   // first pixel CENTRE
    float fe0_row = (sx[2]-sx[1])*(cpy-sy[1]) - (sy[2]-sy[1])*(cpx-sx[1]);
    float fe1_row = (sx[0]-sx[2])*(cpy-sy[2]) - (sy[0]-sy[2])*(cpx-sx[2]);
    float fe2_row = (sx[1]-sx[0])*(cpy-sy[0]) - (sy[1]-sy[0])*(cpx-sx[0]);
    if (sign < 0) {
      fA0=-fA0; fB0=-fB0; fe0_row=-fe0_row;
      fA1=-fA1; fB1=-fB1; fe1_row=-fe1_row;
      fA2=-fA2; fB2=-fB2; fe2_row=-fe2_row;
    }
    for (int y = miny; y <= maxy; y++) {
      float fe0 = fe0_row, fe1 = fe1_row, fe2 = fe2_row;
      at.row_reset();
      for (int x = minx; x <= maxx; x++) {
        if (fe0 >= 0.0f && fe1 >= 0.0f && fe2 >= 0.0f) {
          if (emit(s, x, y, at, at.base(cc))) written++;
        }
        fe0 += fA0; fe1 += fA1; fe2 += fA2;
        at.px_advance();
      }
      fe0_row += fB0; fe1_row += fB1; fe2_row += fB2;
      at.row_advance();
    }
#endif

    return written;
  }

}
