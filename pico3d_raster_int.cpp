// pico3d triangle rasteriser — INTEGER / fixed-point variant.
//
// Drop-in replacement for pico3d_raster.cpp (same exported symbols), selected at
// build time with -DPICO3D_RASTER_INT. The float version stays the default.
//
// Goal: kill the per-pixel FPU dependency-chain latency the benchmark exposed
// (~100+ cyc/pixel). The PER-PIXEL hot path here is PURE INTEGER — coverage,
// attribute stepping, depth, texel addressing and modulate are all int adds/
// shifts. Strictly int32 (NO int64 — its libgcc divide helper has bricked boot
// before). Sub-pixel-accurate and watertight: integer edge stepping is exact, so
// (unlike the float path) there is no accumulation drift.
//
// The per-TRIANGLE setup still uses float (projection + the gradient divides need
// it to avoid int64 overflow), then converts the increments to fixed-point — so
// this targets fill cost (cyc/px), not the geometry setup (cyc/tri). The only
// per-pixel float is the perspective reciprocal (perspective UV only; affine is
// pure integer). Normal mapping is NOT supported here (falls back unlit-ish).

#include "pico3d.hpp"

#include <algorithm>
#include <cmath>

namespace picovector {

  using std::min;
  using std::max;

  static constexpr int PICO3D_AFFINE_MAX = 24;

  // fixed-point scales (all chosen to stay in int32 at 320x240):
  //   coverage edge functions  x16   (4 sub-pixel bits)
  //   depth (0..65535)         x256  (.8)   -> d16 = dep >> 8
  //   colour (0..255)          x256  (.8)
  //   affine u,v (0..~1)       x65536(.16)  -> texel = (u * dim) >> 16
  static constexpr int COV_FX = 16;

  // (A SRAM-texture experiment lived here: it showed the textured-fill cost is
  // the sampling ALU, not the PSRAM texel fetch — cache-sized textures are
  // resident in the 16KB XIP cache, so SRAM residency saved only ~2-3 cyc/px.)

  // ---- helpers -------------------------------------------------------------

  static inline int tex_wrap(int a, int n) {
    if ((n & (n - 1)) == 0) return a & (n - 1);   // power-of-two: mask
    a %= n; return a < 0 ? a + n : a;
  }

  static inline uint32_t tex_nearest_xy(const pico3d_texture_t *t, int tx, int ty) {
    return t->texels[tex_wrap(ty, t->height) * t->width + tex_wrap(tx, t->width)];
  }

  // integer channel lerp, w in [0,256]
  static inline uint32_t lerp_rgb_i(uint32_t a, uint32_t b, int w) {
    int ar = a & 0xff, ag = (a >> 8) & 0xff, ab = (a >> 16) & 0xff;
    int br = b & 0xff, bg = (b >> 8) & 0xff, bb = (b >> 16) & 0xff;
    int r = ar + (((br - ar) * w) >> 8);
    int g = ag + (((bg - ag) * w) >> 8);
    int bl = ab + (((bb - ab) * w) >> 8);
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)bl << 16);
  }

  // bilinear from .8 fixed texel coords (integer part = texel, low 8 bits = frac)
  static inline uint32_t tex_bilinear_fx(const pico3d_texture_t *t, int txf, int tyf) {
    int x0 = (txf >> 8), y0 = (tyf >> 8);
    int wx = txf & 0xff, wy = tyf & 0xff;
    int x0w = tex_wrap(x0, t->width),  x1w = tex_wrap(x0 + 1, t->width);
    int y0w = tex_wrap(y0, t->height), y1w = tex_wrap(y0 + 1, t->height);
    uint32_t a = lerp_rgb_i(t->texels[y0w * t->width + x0w], t->texels[y0w * t->width + x1w], wx);
    uint32_t b = lerp_rgb_i(t->texels[y1w * t->width + x0w], t->texels[y1w * t->width + x1w], wx);
    return lerp_rgb_i(a, b, wy);
  }

  // Screen-affine attributes, all stepped with INTEGER fixed-point adds.
  struct attrs_t {
    bool uvs, varying, persp;
    int32_t dep, ddep_dx, ddep_dy, dep_row;            // depth .8
    int32_t r, dr_dx, dr_dy, r_row;                    // colour .8
    int32_t g, dg_dx, dg_dy, g_row;
    int32_t b, db_dx, db_dy, b_row;
    int32_t u, du_dx, du_dy, u_row;                    // affine u .16
    int32_t v, dv_dx, dv_dy, v_row;
    // Perspective u/w, v/w, 1/w stay FLOAT: the textured bottleneck is the PSRAM
    // texel fetch, not this divide, and one shared float reciprocal (1/iw) beat
    // two integer divides on the M33 (measured: 208 vs 217.6 cyc/px). Keeping the
    // float reciprocal here means this is the only per-pixel float, perspective-only.
    float uw, duwdx, duwdy, uw_row;
    float vw, dvwdx, dvwdy, vw_row;
    float iw, diwdx, diwdy, iw_row;

    void row_reset() {
      dep = dep_row;
      if (varying) { r = r_row; g = g_row; b = b_row; }
      if (uvs) { if (persp) { uw = uw_row; vw = vw_row; iw = iw_row; }
                 else        { u = u_row;  v = v_row; } }
    }
    void row_advance() {
      dep_row += ddep_dy;
      if (varying) { r_row += dr_dy; g_row += dg_dy; b_row += db_dy; }
      if (uvs) { if (persp) { uw_row += duwdy; vw_row += dvwdy; iw_row += diwdy; }
                 else        { u_row += du_dy; v_row += dv_dy; } }
    }
    void px_advance() {
      dep += ddep_dx;
      if (varying) { r += dr_dx; g += dg_dx; b += db_dx; }
      if (uvs) { if (persp) { uw += duwdx; vw += dvwdx; iw += diwdx; }
                 else        { u += du_dx; v += dv_dx; } }
    }
    uint32_t base(uint32_t cc) const {
      if (!varying) return cc;
      int R = r >> 8, G = g >> 8, B = b >> 8;
      R = R < 0 ? 0 : (R > 255 ? 255 : R);
      G = G < 0 ? 0 : (G > 255 ? 255 : G);
      B = B < 0 ? 0 : (B > 255 ? 255 : B);
      return (uint32_t)R | ((uint32_t)G << 8) | ((uint32_t)B << 16);
    }
  };

  struct shade_t {
    pico3d_target_t        *t;
    const pico3d_material_t *m;
    const pico3d_texture_t  *tx;        // texture to sample (PSRAM, or SRAM shadow under test)
    bool tex, persp, bilinear;
    bool white;                         // base colour is white => modulate is identity, skip it
  };

  // MUST stay inlined: with two call sites (the perspective + plain scanlines)
  // GCC otherwise emits emit() as a real function and every pixel pays the call
  // overhead (measured: floor 49.6 -> 90.7 cyc/px). Force it.
  __attribute__((always_inline)) static inline
  bool emit(const shade_t &s, int x, int y, const attrs_t &at, uint32_t base) {
    uint16_t d16 = 0;
    int didx = y * s.t->depth_stride + x;
    if (s.t->depth) {
      int dd = at.dep >> 8;
      d16 = (uint16_t)(dd < 0 ? 0 : (dd > 65535 ? 65535 : dd));
      if (d16 >= s.t->depth[didx]) return false;
    }

    uint32_t col = base;
    if (s.tex) {
      int txf, tyf;                       // .8 fixed texel coords
      if (s.persp) {
        float rw = 1.0f / at.iw;          // the ONE per-pixel float (perspective only)
        txf = (int)(at.uw * rw * s.tx->width  * 256.0f);
        tyf = (int)(at.vw * rw * s.tx->height * 256.0f);
      } else {
        // affine: u,v are .16; (u * dim) >> 8 = .8 texel coord. int32-safe for
        // UVs up to ~100x tiling (u.16 ~6.5e6 * dim 256 < 2^31).
        txf = (at.u * s.tx->width)  >> 8;
        tyf = (at.v * s.tx->height) >> 8;
      }
      uint32_t px = s.bilinear ? tex_bilinear_fx(s.tx, txf, tyf)
                               : tex_nearest_xy(s.tx, txf >> 8, tyf >> 8);
      if (((px >> 24) & 0xffu) < s.m->alpha_cutoff) return false;
      // White base => (255*texel)/255 == texel, so the per-channel modulate is
      // an identity multiply (~20 cyc/px wasted). Skip it for white materials.
      col = s.white ? (px & 0xffffffu) : pico3d_modulate(col, px);
    }
    s.t->color[y * s.t->color_stride + x] = col;
    if (s.t->depth) s.t->depth[didx] = d16;
    return true;
  }

  // ---- entry points --------------------------------------------------------

  void pico3d_depth_clear(pico3d_target_t *t, uint16_t value) {
    if (!t->depth) return;
    for (int y = 0; y < t->height; y++) {
      uint16_t *row = t->depth + y * t->depth_stride;
      for (int x = 0; x < t->width; x++) row[x] = value;
    }
  }

  int pico3d_raster_triangle(pico3d_target_t *t, const pico3d_tri_t *tri,
                             const pico3d_material_t *m, const pico3d_light_t *light) {
    (void)light;   // integer variant does not implement per-pixel lighting (normal maps)

    // --- float setup (per triangle): project, area, gradients -----------------
    float sx[3], sy[3], z[3], iw[3], uw[3], vw[3], uv0[3], uv1[3];
    uint32_t rgb[3];
    for (int i = 0; i < 3; i++) {
      float w = 1.0f / tri->clip[i].w;
      sx[i] = (tri->clip[i].x * w * 0.5f + 0.5f) * t->width;
      sy[i] = (1.0f - (tri->clip[i].y * w * 0.5f + 0.5f)) * t->height;
      z[i]  = tri->clip[i].z * w;
      iw[i] = w;
      uv0[i] = tri->uv_[i].x; uv1[i] = tri->uv_[i].y;
      uw[i] = uv0[i] * w; vw[i] = uv1[i] * w;
      rgb[i] = tri->rgb[i];
    }

    float denomf = (sx[1]-sx[0]) * (sy[2]-sy[0]) - (sy[1]-sy[0]) * (sx[2]-sx[0]);
    if (denomf == 0.0f) return 0;
    int sign;
    if (denomf < 0.0f) { sign = -1; }
    else { if (!m->double_sided) return 0; sign = 1; }

    float minxf = min(sx[0], min(sx[1], sx[2])), maxxf = max(sx[0], max(sx[1], sx[2]));
    float minyf = min(sy[0], min(sy[1], sy[2])), maxyf = max(sy[0], max(sy[1], sy[2]));
    int minx = max(t->clip_x0, (int)floorf(minxf));
    int maxx = min(t->clip_x1 - 1, (int)floorf(maxxf) + 1);
    int miny = max(t->clip_y0, (int)floorf(minyf));
    int maxy = min(t->clip_y1 - 1, (int)floorf(maxyf) + 1);
    if (minx > maxx || miny > maxy) return 0;

    float invD = 1.0f / denomf;
    float dx1 = sx[1]-sx[0], dy1 = sy[1]-sy[0];
    float dx2 = sx[2]-sx[0], dy2 = sy[2]-sy[0];
    float px0 = (float)minx + 0.5f, py0 = (float)miny + 0.5f;
    // plane(): float gradients of an attribute, returned as (ddx, ddy, row@first-px-centre)
    auto plane = [&](float a0, float a1, float a2, float &ddx, float &ddy, float &row) {
      float d1 = a1 - a0, d2 = a2 - a0;
      ddx = (d1 * dy2 - d2 * dy1) * invD;
      ddy = (d2 * dx1 - d1 * dx2) * invD;
      row = a0 + ddx * (px0 - sx[0]) + ddy * (py0 - sy[0]);
    };

    attrs_t at{};
    bool has_tex = (m->texture != nullptr);
    at.varying = !(rgb[0] == rgb[1] && rgb[1] == rgb[2]);
    at.uvs = has_tex;
    at.persp = at.uvs && ((maxx - minx) > PICO3D_AFFINE_MAX || (maxy - miny) > PICO3D_AFFINE_MAX);
    uint32_t cc = rgb[0];

    float fddx, fddy, frow;
    // depth: map NDC z -> 0..65535, carry as .8 fixed
    plane(z[0], z[1], z[2], fddx, fddy, frow);
    const float DEPTH = 65535.0f * 0.5f, DBASE = 65535.0f * 0.5f;
    at.dep_row  = (int32_t)((frow * DEPTH + DBASE) * 256.0f);
    at.ddep_dx  = (int32_t)(fddx * DEPTH * 256.0f);
    at.ddep_dy  = (int32_t)(fddy * DEPTH * 256.0f);

    if (at.uvs) {
      if (at.persp) {
        plane(iw[0], iw[1], iw[2], at.diwdx, at.diwdy, at.iw_row);
        plane(uw[0], uw[1], uw[2], at.duwdx, at.duwdy, at.uw_row);
        plane(vw[0], vw[1], vw[2], at.dvwdx, at.dvwdy, at.vw_row);
      } else {
        plane(uv0[0], uv0[1], uv0[2], fddx, fddy, frow);   // affine u (.16)
        at.u_row = (int32_t)(frow * 65536.0f); at.du_dx = (int32_t)(fddx * 65536.0f); at.du_dy = (int32_t)(fddy * 65536.0f);
        plane(uv1[0], uv1[1], uv1[2], fddx, fddy, frow);
        at.v_row = (int32_t)(frow * 65536.0f); at.dv_dx = (int32_t)(fddx * 65536.0f); at.dv_dy = (int32_t)(fddy * 65536.0f);
      }
    }
    if (at.varying) {
      plane((float)(rgb[0]&0xff),(float)(rgb[1]&0xff),(float)(rgb[2]&0xff), fddx,fddy,frow);
      at.r_row=(int32_t)(frow*256.0f); at.dr_dx=(int32_t)(fddx*256.0f); at.dr_dy=(int32_t)(fddy*256.0f);
      plane((float)((rgb[0]>>8)&0xff),(float)((rgb[1]>>8)&0xff),(float)((rgb[2]>>8)&0xff), fddx,fddy,frow);
      at.g_row=(int32_t)(frow*256.0f); at.dg_dx=(int32_t)(fddx*256.0f); at.dg_dy=(int32_t)(fddy*256.0f);
      plane((float)((rgb[0]>>16)&0xff),(float)((rgb[1]>>16)&0xff),(float)((rgb[2]>>16)&0xff), fddx,fddy,frow);
      at.b_row=(int32_t)(frow*256.0f); at.db_dx=(int32_t)(fddx*256.0f); at.db_dy=(int32_t)(fddy*256.0f);
    }

    // --- coverage: float edge fns at pixel centres, converted to int (x16) -----
    float fA0=(sy[1]-sy[2]), fB0=(sx[2]-sx[1]);
    float fA1=(sy[2]-sy[0]), fB1=(sx[0]-sx[2]);
    float fA2=(sy[0]-sy[1]), fB2=(sx[1]-sx[0]);
    float cpx=(float)minx+0.5f, cpy=(float)miny+0.5f;
    float fe0=(sx[2]-sx[1])*(cpy-sy[1]) - (sy[2]-sy[1])*(cpx-sx[1]);
    float fe1=(sx[0]-sx[2])*(cpy-sy[2]) - (sy[0]-sy[2])*(cpx-sx[2]);
    float fe2=(sx[1]-sx[0])*(cpy-sy[0]) - (sy[1]-sy[0])*(cpx-sx[0]);
    if (sign < 0) { fA0=-fA0;fB0=-fB0;fe0=-fe0; fA1=-fA1;fB1=-fB1;fe1=-fe1; fA2=-fA2;fB2=-fB2;fe2=-fe2; }
    // (int) truncation, not lroundf (a libm CALL ~9/triangle): vcvt is 1 cyc and
    // truncation-toward-zero is symmetric, so shared edges stay watertight.
    int32_t A0=(int32_t)(fA0*COV_FX), B0=(int32_t)(fB0*COV_FX), e0_row=(int32_t)(fe0*COV_FX);
    int32_t A1=(int32_t)(fA1*COV_FX), B1=(int32_t)(fB1*COV_FX), e1_row=(int32_t)(fe1*COV_FX);
    int32_t A2=(int32_t)(fA2*COV_FX), B2=(int32_t)(fB2*COV_FX), e2_row=(int32_t)(fe2*COV_FX);

    shade_t s{};
    s.t = t; s.m = m; s.tex = has_tex; s.persp = at.persp;
    s.bilinear = has_tex && (m->filter == PICO3D_BILINEAR);
    // base white (unlit white material, constant verts) => modulate is identity
    s.white = has_tex && !at.varying && ((cc & 0xffffffu) == 0xffffffu);
    s.tx = m->texture;

    int written = 0;
    for (int y = miny; y <= maxy; y++) {
      int32_t e0 = e0_row, e1 = e1_row, e2 = e2_row;
      at.row_reset();
      for (int x = minx; x <= maxx; x++) {
        if ((e0 | e1 | e2) >= 0) {              // all >= 0 (sign bit clear)
          if (emit(s, x, y, at, at.base(cc))) written++;
        }
        e0 += A0; e1 += A1; e2 += A2;
        at.px_advance();
      }
      e0_row += B0; e1_row += B1; e2_row += B2;
      at.row_advance();
    }
    return written;
  }

}
