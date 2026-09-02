// pico3d triangle rasteriser — INTEGER, TEMPLATE-SPECIALISED variant.
//
// Drop-in replacement for pico3d_raster.cpp (same exported symbols), selected at
// build time with PICO3D_RASTER=int_templated in picovector.cmake.
//
// Same integer hot path as pico3d_raster_int.cpp, but the per-pixel feature
// branches (textured / vertex-coloured / normal-mapped / depth) are resolved at
// COMPILE time: one templated scanline body raster_fill<TEX,VARYING,NMAP,DEPTH>
// with `if constexpr` gating each block + its attribute stepping, dispatched from
// a 16-way switch on the runtime flags. Each instantiation does ONLY the work it
// needs, branch-free. emit() is force-inlined into it.
//
//   * Non-normal-mapped paths: pure integer (fixed-point), like pico3d_raster_int.
//   * Normal-mapped paths: a faithful float port of pico3d_raster.cpp's per-pixel
//     TBN lighting (so the int path gains normal mapping). Uses vec3 helpers only
//     (the boot-safe form — never raw sqrtf; see the engine notes).
//
// Runtime (set-once, well-predicted) branches kept inside the templates: affine
// vs perspective UV, nearest vs bilinear, and the white-base modulate skip.

#include "pico3d.hpp"

#include <algorithm>
#include <cmath>

// Pin the rasteriser hot path to SRAM on-device. The RP2350 executes code from
// external flash via the XIP cache; the ~22 KB per-triangle function (all 16 fill
// specialisations inlined) does not fit, so it thrashes — which is what makes the
// per-feature timings noisy/counter-intuitive on-device but not on host. The pico-sdk
// __not_in_flash_func attribute relocates it to the .time_critical RAM section
// (copied from flash at boot). On host (no pico-sdk) it degrades to a plain name.
#if __has_include("pico.h")
#include "pico.h"            // pulls in pico/platform.h -> __not_in_flash_func
#endif
#ifndef __not_in_flash_func
#define __not_in_flash_func(f) f
#endif

namespace picovector {

  using std::min;
  using std::max;

  static constexpr int PICO3D_AFFINE_MAX = 24;
  static constexpr int COV_FX = 16;     // edge-function fixed-point (x16)

  // ---- helpers -------------------------------------------------------------

  static inline int tex_wrap(int a, int n) {
    if ((n & (n - 1)) == 0) return a & (n - 1);   // PoT: mask
    a %= n; return a < 0 ? a + n : a;
  }

  // integer samplers (non-nmap fast path) ------------------------------------
  static inline uint32_t tex_nearest_xy(const pico3d_texture_t *t, int tx, int ty) {
    return t->texels[tex_wrap(ty, t->height) * t->width + tex_wrap(tx, t->width)];
  }
  static inline uint32_t lerp_rgb_i(uint32_t a, uint32_t b, int w) {   // w in [0,256]
    int ar = a & 0xff, ag = (a >> 8) & 0xff, ab = (a >> 16) & 0xff, aa = (a >> 24) & 0xff;
    int br = b & 0xff, bg = (b >> 8) & 0xff, bb = (b >> 16) & 0xff, ba = (b >> 24) & 0xff;
    int r = ar + (((br - ar) * w) >> 8);
    int g = ag + (((bg - ag) * w) >> 8);
    int bl = ab + (((bb - ab) * w) >> 8);
    int al = aa + (((ba - aa) * w) >> 8);                  // blend alpha too, else the
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)bl << 16) | ((uint32_t)al << 24);
  }                                                         // alpha-cutoff test kills it
  static inline uint32_t __not_in_flash_func(tex_bilinear_fx)(const pico3d_texture_t *t, int txf, int tyf) {
    int x0 = (txf >> 8), y0 = (tyf >> 8);
    int wx = txf & 0xff, wy = tyf & 0xff;
    int x0w = tex_wrap(x0, t->width),  x1w = tex_wrap(x0 + 1, t->width);
    int y0w = tex_wrap(y0, t->height), y1w = tex_wrap(y0 + 1, t->height);
    uint32_t a = lerp_rgb_i(t->texels[y0w * t->width + x0w], t->texels[y0w * t->width + x1w], wx);
    uint32_t b = lerp_rgb_i(t->texels[y1w * t->width + x0w], t->texels[y1w * t->width + x1w], wx);
    return lerp_rgb_i(a, b, wy);
  }

  // float samplers (normal-map path; float u,v) ------------------------------
  static inline uint32_t tex_nearest_f(const pico3d_texture_t *t, float u, float v) {
    int x = tex_wrap((int)(u * t->width), t->width);
    int y = tex_wrap((int)(v * t->height), t->height);
    return t->texels[y * t->width + x];
  }
  static inline uint32_t lerp_rgb_f(uint32_t a, uint32_t b, float t) {
    int ar = a & 0xff, ag = (a >> 8) & 0xff, ab = (a >> 16) & 0xff, aa = (a >> 24) & 0xff;
    int br = b & 0xff, bg = (b >> 8) & 0xff, bb = (b >> 16) & 0xff, ba = (b >> 24) & 0xff;
    int r = ar + (int)((br - ar) * t), g = ag + (int)((bg - ag) * t), bl = ab + (int)((bb - ab) * t);
    int al = aa + (int)((ba - aa) * t);                    // keep alpha for the cutoff test
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)bl << 16) | ((uint32_t)al << 24);
  }
  static inline uint32_t __not_in_flash_func(tex_bilinear_f)(const pico3d_texture_t *t, float u, float v) {
    float fx = u * t->width - 0.5f, fy = v * t->height - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float tx = fx - x0, ty = fy - y0;
    int x1 = tex_wrap(x0 + 1, t->width), y1 = tex_wrap(y0 + 1, t->height);
    x0 = tex_wrap(x0, t->width); y0 = tex_wrap(y0, t->height);
    uint32_t a = lerp_rgb_f(t->texels[y0 * t->width + x0], t->texels[y0 * t->width + x1], tx);
    uint32_t b = lerp_rgb_f(t->texels[y1 * t->width + x0], t->texels[y1 * t->width + x1], tx);
    return lerp_rgb_f(a, b, ty);
  }

  // Per-pixel attributes. Integer fixed-point for the hot fields (depth .8,
  // colour .8, affine u,v .16); float for perspective u/w,v/w,1/w and for the
  // normal-map world normal/tangent. Plain data — stepping lives in raster_fill.
  struct attrs_t {
    bool persp;
    int32_t dep, ddep_dx, ddep_dy, dep_row;
    int32_t r, dr_dx, dr_dy, r_row;
    int32_t g, dg_dx, dg_dy, g_row;
    int32_t b, db_dx, db_dy, b_row;
    int32_t u, du_dx, du_dy, u_row;                    // affine u (.16)
    int32_t v, dv_dx, dv_dy, v_row;
    float uw, duwdx, duwdy, uw_row;                    // perspective u/w
    float vw, dvwdx, dvwdy, vw_row;
    float iw, diwdx, diwdy, iw_row;                    // 1/w
    float nrm[3], nrm_dx[3], nrm_dy[3], nrm_row[3];    // world normal (nmap)
    float tan[3], tan_dx[3], tan_dy[3], tan_row[3];    // world tangent (nmap)
  };

  struct edges_t { int32_t A0,A1,A2,B0,B1,B2,e0_row,e1_row,e2_row; };

  struct shade_t {
    pico3d_target_t        *t;
    const pico3d_material_t *m;
    const pico3d_light_t   *light;
    const pico3d_texture_t *tx;
    vec3_t L;                       // -light->direction (normalised); nmap only
    bool persp, bilinear, white;
  };

  // Current-pixel running values — a SMALL cursor kept in registers by raster_fill.
  // We deliberately do NOT pass the big attrs_t into the loop: by value it copies
  // ~240 bytes per triangle (murders the small-triangle geometry profile); by
  // reference its int32 fields can alias the uint32 colour write, forcing per-pixel
  // reloads. Instead raster_fill hoists the (used) steps to locals and steps this.
  struct cur_t {
    int32_t dep, r, g, b, u, v;
    float uw, vw, iw;
    float nrm[3], tan[3];
  };

  // ---- specialised pixel + scanline ---------------------------------------

  template<bool TEX, bool VARYING, bool NMAP, bool DEPTH>
  __attribute__((always_inline)) static inline
  bool emit(const shade_t &s, int x, int y, const cur_t &c, uint32_t cc) {
    uint16_t d16 = 0;
    int didx = y * s.t->depth_stride + x;
    if constexpr (DEPTH) {
      int dd = c.dep >> 8;
      d16 = (uint16_t)(dd < 0 ? 0 : (dd > 65535 ? 65535 : dd));
      if (d16 >= s.t->depth[didx]) return false;
    }

    uint32_t col;
    if constexpr (VARYING) {
      int R = c.r >> 8, G = c.g >> 8, B = c.b >> 8;
      R = R < 0 ? 0 : (R > 255 ? 255 : R);
      G = G < 0 ? 0 : (G > 255 ? 255 : G);
      B = B < 0 ? 0 : (B > 255 ? 255 : B);
      col = (uint32_t)R | ((uint32_t)G << 8) | ((uint32_t)B << 16);
    } else col = cc;

    if constexpr (NMAP) {
      // float port of the per-pixel TBN normal-map lighting (vec3 helpers only).
      // (Matcap no longer comes through here — it's a plain textured triangle now,
      // with its sphere-map UV baked per vertex in the draw stage.)
      float u, v;
      if (s.persp) { float rw = 1.0f / c.iw; u = c.uw * rw; v = c.vw * rw; }
      else         { u = c.u * (1.0f / 65536.0f); v = c.v * (1.0f / 65536.0f); }
      if constexpr (TEX) {
        uint32_t px = s.bilinear ? tex_bilinear_f(s.tx, u, v) : tex_nearest_f(s.tx, u, v);
        if (((px >> 24) & 0xffu) < s.m->alpha_cutoff) return false;
        col = s.white ? (px & 0xffffffu) : pico3d_modulate(col, px);
      }
      // Per-pixel normal: TBN-perturbed when a normal map is bound, else the plain
      // interpolated vertex normal (the specular-only path).
      vec3_t wn(c.nrm[0], c.nrm[1], c.nrm[2]);
      if (s.m->normal_map) {
        vec3_t T(c.tan[0], c.tan[1], c.tan[2]);
        vec3_t Bv = wn.cross(T);
        uint32_t np = s.bilinear ? tex_bilinear_f(s.m->normal_map, u, v)
                                 : tex_nearest_f(s.m->normal_map, u, v);
        float mx = (float)(np        & 0xff) * (1.0f / 127.5f) - 1.0f;
        float my = (float)((np >> 8)  & 0xff) * (1.0f / 127.5f) - 1.0f;
        float mz = (float)((np >> 16) & 0xff) * (1.0f / 127.5f) - 1.0f;
        wn = (T * mx + Bv * my + wn * mz).normalized();
      } else {
        wn = wn.normalized();
      }
      col = pico3d_modulate(col, pico3d_light_value(s.light, wn.dot(s.L)));
      if (s.m->specular)                                  // Blinn-Phong highlight on top
        col = pico3d_add_specular(col, s.m->specular, wn.dot(s.light->half), s.m->shininess);
    } else if constexpr (TEX) {
      int txf, tyf;
      if (s.persp) {
        float rw = 1.0f / c.iw;
        txf = (int)(c.uw * rw * s.tx->width  * 256.0f);
        tyf = (int)(c.vw * rw * s.tx->height * 256.0f);
      } else {
        txf = (c.u * s.tx->width)  >> 8;
        tyf = (c.v * s.tx->height) >> 8;
      }
      uint32_t px = s.bilinear ? tex_bilinear_fx(s.tx, txf, tyf)
                               : tex_nearest_xy(s.tx, txf >> 8, tyf >> 8);
      if (((px >> 24) & 0xffu) < s.m->alpha_cutoff) return false;
      col = s.white ? (px & 0xffffffu) : pico3d_modulate(col, px);
    }

    s.t->color[y * s.t->color_stride + x] = col;
    if constexpr (DEPTH) s.t->depth[didx] = d16;
    return true;
  }

  // at is by CONST REF (no per-triangle copy). The USED steps + row starts are
  // hoisted to locals once, then the scanline steps a small cur_t — so the loop
  // touches only locals (registers; no aliasing with the colour write) and the
  // per-triangle cost is a few words, not a 240-byte struct copy.
  template<bool TEX, bool VARYING, bool NMAP, bool DEPTH>
  __attribute__((always_inline)) static inline
  int raster_fill(const shade_t &s, const attrs_t &at, uint32_t cc,
                  int minx, int maxx, int miny, int maxy, edges_t ed) {
    constexpr bool UVS = TEX || NMAP;
    const bool persp = s.persp;

    const int32_t ddep_dx = at.ddep_dx, ddep_dy = at.ddep_dy;
    int32_t dep_row = at.dep_row;
    int32_t dr_dx=0,dr_dy=0,dg_dx=0,dg_dy=0,db_dx=0,db_dy=0, r_row=0,g_row=0,b_row=0;
    if constexpr (VARYING) {
      dr_dx=at.dr_dx; dr_dy=at.dr_dy; dg_dx=at.dg_dx; dg_dy=at.dg_dy; db_dx=at.db_dx; db_dy=at.db_dy;
      r_row=at.r_row; g_row=at.g_row; b_row=at.b_row;
    }
    int32_t du_dx=0,du_dy=0,dv_dx=0,dv_dy=0, u_row=0,v_row=0;
    float duwdx=0,duwdy=0,dvwdx=0,dvwdy=0,diwdx=0,diwdy=0, uw_row=0,vw_row=0,iw_row=0;
    if constexpr (UVS) {
      if (persp) { duwdx=at.duwdx;duwdy=at.duwdy;dvwdx=at.dvwdx;dvwdy=at.dvwdy;diwdx=at.diwdx;diwdy=at.diwdy;
                   uw_row=at.uw_row;vw_row=at.vw_row;iw_row=at.iw_row; }
      else       { du_dx=at.du_dx;du_dy=at.du_dy;dv_dx=at.dv_dx;dv_dy=at.dv_dy; u_row=at.u_row;v_row=at.v_row; }
    }
    float nrm_dx[3]={0},nrm_dy[3]={0},nrm_row[3]={0},tan_dx[3]={0},tan_dy[3]={0},tan_row[3]={0};
    if constexpr (NMAP) for (int k=0;k<3;k++){ nrm_dx[k]=at.nrm_dx[k];nrm_dy[k]=at.nrm_dy[k];nrm_row[k]=at.nrm_row[k];
                                               tan_dx[k]=at.tan_dx[k];tan_dy[k]=at.tan_dy[k];tan_row[k]=at.tan_row[k]; }

    int written = 0;
    cur_t c{};   // value-init once (silences -Wmaybe-uninitialized; DCE'd where set before use)
    for (int y = miny; y <= maxy; y++) {
      int32_t e0 = ed.e0_row, e1 = ed.e1_row, e2 = ed.e2_row;
      c.dep = dep_row;
      if constexpr (VARYING) { c.r=r_row; c.g=g_row; c.b=b_row; }
      if constexpr (UVS) { if (persp) { c.uw=uw_row; c.vw=vw_row; c.iw=iw_row; }
                           else        { c.u=u_row; c.v=v_row; } }
      if constexpr (NMAP) for (int k=0;k<3;k++){ c.nrm[k]=nrm_row[k]; c.tan[k]=tan_row[k]; }

      for (int x = minx; x <= maxx; x++) {
        if ((e0 | e1 | e2) >= 0) {
          if (emit<TEX,VARYING,NMAP,DEPTH>(s, x, y, c, cc)) written++;
        }
        e0 += ed.A0; e1 += ed.A1; e2 += ed.A2;
        c.dep += ddep_dx;
        if constexpr (VARYING) { c.r+=dr_dx; c.g+=dg_dx; c.b+=db_dx; }
        if constexpr (UVS) { if (persp) { c.uw+=duwdx; c.vw+=dvwdx; c.iw+=diwdx; }
                             else        { c.u+=du_dx; c.v+=dv_dx; } }
        if constexpr (NMAP) for (int k=0;k<3;k++){ c.nrm[k]+=nrm_dx[k]; c.tan[k]+=tan_dx[k]; }
      }

      ed.e0_row += ed.B0; ed.e1_row += ed.B1; ed.e2_row += ed.B2;
      dep_row += ddep_dy;
      if constexpr (VARYING) { r_row+=dr_dy; g_row+=dg_dy; b_row+=db_dy; }
      if constexpr (UVS) { if (persp) { uw_row+=duwdy; vw_row+=dvwdy; iw_row+=diwdy; }
                           else        { u_row+=du_dy; v_row+=dv_dy; } }
      if constexpr (NMAP) for (int k=0;k<3;k++){ nrm_row[k]+=nrm_dy[k]; tan_row[k]+=tan_dy[k]; }
    }
    return written;
  }

  // ---- entry points --------------------------------------------------------

  void pico3d_depth_clear(pico3d_target_t *t, uint16_t value) {
    if (!t->depth) return;
    for (int y = 0; y < t->height; y++) {
      uint16_t *row = t->depth + y * t->depth_stride;
      for (int x = 0; x < t->width; x++) row[x] = value;
    }
  }

#if PICO3D_PROF
  #define RT_SETUP_END() (pico3d_prof_project_cyc += pico3d_prof_cyc() - ps)   // cull early-outs count as project
#else
  #define RT_SETUP_END() ((void)0)
#endif

  int __not_in_flash_func(pico3d_raster_triangle)(pico3d_target_t *t, const pico3d_tri_t *tri,
                             const pico3d_material_t *m, const pico3d_light_t *light) {
#if PICO3D_PROF
    uint32_t ps = pico3d_prof_cyc();
#endif
    // --- float setup (per triangle) ------------------------------------------
    float sx[3], sy[3], z[3], iw[3], uw[3], vw[3], uv0[3], uv1[3];
    uint32_t rgb[3];
    for (int i = 0; i < 3; i++) {
      sx[i] = tri->sx[i]; sy[i] = tri->sy[i]; z[i] = tri->z[i]; iw[i] = tri->iw[i];
      uv0[i] = tri->uv_[i].x; uv1[i] = tri->uv_[i].y;
      uw[i] = uv0[i] * iw[i]; vw[i] = uv1[i] * iw[i];   // perspective uv (iw is cached)
      rgb[i] = tri->rgb[i];
    }

    float denomf = (sx[1]-sx[0]) * (sy[2]-sy[0]) - (sy[1]-sy[0]) * (sx[2]-sx[0]);
    if (denomf == 0.0f) { RT_SETUP_END(); return 0; }
    int sign;
    if (denomf < 0.0f) sign = -1;
    else { if (!m->double_sided) { RT_SETUP_END(); return 0; } sign = 1; }

    float minxf = min(sx[0], min(sx[1], sx[2])), maxxf = max(sx[0], max(sx[1], sx[2]));
    float minyf = min(sy[0], min(sy[1], sy[2])), maxyf = max(sy[0], max(sy[1], sy[2]));
    int minx = max(t->clip_x0, (int)floorf(minxf));
    int maxx = min(t->clip_x1 - 1, (int)floorf(maxxf) + 1);
    int miny = max(t->clip_y0, (int)floorf(minyf));
    int maxy = min(t->clip_y1 - 1, (int)floorf(maxyf) + 1);
    if (minx > maxx || miny > maxy) { RT_SETUP_END(); return 0; }
#if PICO3D_PROF
    uint32_t tp = pico3d_prof_cyc(); pico3d_prof_project_cyc += tp - ps;   // project done
#endif

    float invD = 1.0f / denomf;
    float dx1 = sx[1]-sx[0], dy1 = sy[1]-sy[0];
    float dx2 = sx[2]-sx[0], dy2 = sy[2]-sy[0];
    float px0 = (float)minx + 0.5f, py0 = (float)miny + 0.5f;
    auto plane = [&](float a0, float a1, float a2, float &ddx, float &ddy, float &row) {
      float d1 = a1 - a0, d2 = a2 - a0;
      ddx = (d1 * dy2 - d2 * dy1) * invD;
      ddy = (d2 * dx1 - d1 * dx2) * invD;
      row = a0 + ddx * (px0 - sx[0]) + ddy * (py0 - sy[0]);
    };

    bool nmap    = ((m->normal_map != nullptr) || (m->specular != 0)) && (light != nullptr);
    // Matcap is a sphere-map texture: the reflection UV is baked per vertex in the
    // draw stage (tri->uv_), so here it's just a textured triangle sampling m->matcap
    // (no per-pixel normal). normal_map still wins if both are set.
    bool matcap  = (m->matcap != nullptr) && !nmap;
    const pico3d_texture_t *tx = m->texture ? m->texture : (matcap ? m->matcap : nullptr);
    bool has_tex = (tx != nullptr);
    bool varying = !(rgb[0] == rgb[1] && rgb[1] == rgb[2]);
    bool uvs     = has_tex || nmap;
    // value-initialised: the if-constexpr-gated reads in raster_fill confuse
    // -Wmaybe-uninitialized (it can't prove the runtime varying/persp/nmap flags
    // match the template params), and removing it gave no measured geometry win.
    // Redundant zero-stores for fields set before use are DCE'd.
    attrs_t at{};
    at.persp = uvs && ((maxx - minx) > PICO3D_AFFINE_MAX || (maxy - miny) > PICO3D_AFFINE_MAX);
    uint32_t cc = rgb[0];

    float fddx, fddy, frow;
    plane(z[0], z[1], z[2], fddx, fddy, frow);
    const float DEPTH = 65535.0f * 0.5f, DBASE = 65535.0f * 0.5f;
    at.dep_row = (int32_t)((frow * DEPTH + DBASE) * 256.0f);
    at.ddep_dx = (int32_t)(fddx * DEPTH * 256.0f);
    at.ddep_dy = (int32_t)(fddy * DEPTH * 256.0f);

    if (uvs) {
      if (at.persp) {
        plane(iw[0], iw[1], iw[2], at.diwdx, at.diwdy, at.iw_row);
        plane(uw[0], uw[1], uw[2], at.duwdx, at.duwdy, at.uw_row);
        plane(vw[0], vw[1], vw[2], at.dvwdx, at.dvwdy, at.vw_row);
      } else {
        plane(uv0[0], uv0[1], uv0[2], fddx, fddy, frow);
        at.u_row=(int32_t)(frow*65536.0f); at.du_dx=(int32_t)(fddx*65536.0f); at.du_dy=(int32_t)(fddy*65536.0f);
        plane(uv1[0], uv1[1], uv1[2], fddx, fddy, frow);
        at.v_row=(int32_t)(frow*65536.0f); at.dv_dx=(int32_t)(fddx*65536.0f); at.dv_dy=(int32_t)(fddy*65536.0f);
      }
    }
    if (varying) {
      plane((float)(rgb[0]&0xff),(float)(rgb[1]&0xff),(float)(rgb[2]&0xff), fddx,fddy,frow);
      at.r_row=(int32_t)(frow*256.0f); at.dr_dx=(int32_t)(fddx*256.0f); at.dr_dy=(int32_t)(fddy*256.0f);
      plane((float)((rgb[0]>>8)&0xff),(float)((rgb[1]>>8)&0xff),(float)((rgb[2]>>8)&0xff), fddx,fddy,frow);
      at.g_row=(int32_t)(frow*256.0f); at.dg_dx=(int32_t)(fddx*256.0f); at.dg_dy=(int32_t)(fddy*256.0f);
      plane((float)((rgb[0]>>16)&0xff),(float)((rgb[1]>>16)&0xff),(float)((rgb[2]>>16)&0xff), fddx,fddy,frow);
      at.b_row=(int32_t)(frow*256.0f); at.db_dx=(int32_t)(fddx*256.0f); at.db_dy=(int32_t)(fddy*256.0f);
    }
    if (nmap) {   // per-pixel TBN normal + tangent (matcap uses neither now)
      plane(tri->n[0].x, tri->n[1].x, tri->n[2].x, at.nrm_dx[0], at.nrm_dy[0], at.nrm_row[0]);
      plane(tri->n[0].y, tri->n[1].y, tri->n[2].y, at.nrm_dx[1], at.nrm_dy[1], at.nrm_row[1]);
      plane(tri->n[0].z, tri->n[1].z, tri->n[2].z, at.nrm_dx[2], at.nrm_dy[2], at.nrm_row[2]);
      plane(tri->tan[0].x, tri->tan[1].x, tri->tan[2].x, at.tan_dx[0], at.tan_dy[0], at.tan_row[0]);
      plane(tri->tan[0].y, tri->tan[1].y, tri->tan[2].y, at.tan_dx[1], at.tan_dy[1], at.tan_row[1]);
      plane(tri->tan[0].z, tri->tan[1].z, tri->tan[2].z, at.tan_dx[2], at.tan_dy[2], at.tan_row[2]);
    }

#if PICO3D_PROF
    uint32_t tpl = pico3d_prof_cyc(); pico3d_prof_planes_cyc += tpl - tp;   // planes done
#endif
    // coverage: float edge fns at pixel centres -> int (x16)
    float fA0=(sy[1]-sy[2]), fB0=(sx[2]-sx[1]);
    float fA1=(sy[2]-sy[0]), fB1=(sx[0]-sx[2]);
    float fA2=(sy[0]-sy[1]), fB2=(sx[1]-sx[0]);
    float cpx=(float)minx+0.5f, cpy=(float)miny+0.5f;
    float fe0=(sx[2]-sx[1])*(cpy-sy[1]) - (sy[2]-sy[1])*(cpx-sx[1]);
    float fe1=(sx[0]-sx[2])*(cpy-sy[2]) - (sy[0]-sy[2])*(cpx-sx[2]);
    float fe2=(sx[1]-sx[0])*(cpy-sy[0]) - (sy[1]-sy[0])*(cpx-sx[0]);
    if (sign < 0) { fA0=-fA0;fB0=-fB0;fe0=-fe0; fA1=-fA1;fB1=-fB1;fe1=-fe1; fA2=-fA2;fB2=-fB2;fe2=-fe2; }
    // (int) truncation, NOT lroundf: lroundf is a libm CALL (~9/triangle = a big
    // chunk of setup); (int) is a 1-cyc vcvt. Truncation-toward-zero is symmetric
    // so shared edges stay exact negations (watertight), and <1 LSB at COV_FX=16
    // is <0.1px (the edge value changes ~100s/px).
    edges_t ed;
    ed.A0=(int32_t)(fA0*COV_FX); ed.B0=(int32_t)(fB0*COV_FX); ed.e0_row=(int32_t)(fe0*COV_FX);
    ed.A1=(int32_t)(fA1*COV_FX); ed.B1=(int32_t)(fB1*COV_FX); ed.e1_row=(int32_t)(fe1*COV_FX);
    ed.A2=(int32_t)(fA2*COV_FX); ed.B2=(int32_t)(fB2*COV_FX); ed.e2_row=(int32_t)(fe2*COV_FX);

    shade_t s{};
    s.t = t; s.m = m; s.light = light; s.tx = tx;   // tx = texture, or the matcap sphere-map
    s.persp = at.persp;
    s.bilinear = (has_tex || nmap) && (m->filter == PICO3D_BILINEAR);
    s.white = has_tex && !varying && ((cc & 0xffffffu) == 0xffffffu);
    if (nmap) s.L = (-light->direction).normalized();

    // dispatch to the branch-free specialisation for this feature combination
    // (matcap is just TEX here, with its UV baked per vertex by the draw stage)
    int key = (has_tex ? 1 : 0) | (varying ? 2 : 0) | (nmap ? 4 : 0) | (t->depth ? 8 : 0);
#if PICO3D_PROF
    uint32_t pf = pico3d_prof_cyc();
    pico3d_prof_edges_cyc += pf - tpl;         // edges (+shade) done; fill (scanline) follows
    pico3d_prof_bbox_px += (uint64_t)(maxx - minx + 1) * (maxy - miny + 1);  // px the fill iterates
#endif
    int written = 0;
    switch (key) {
#define C(k,T,V,N,D) case k: written = raster_fill<T,V,N,D>(s, at, cc, minx, maxx, miny, maxy, ed); break;
      C(0,false,false,false,false) C(1,true,false,false,false) C(2,false,true,false,false) C(3,true,true,false,false)
      C(4,false,false,true,false)  C(5,true,false,true,false)  C(6,false,true,true,false)  C(7,true,true,true,false)
      C(8,false,false,false,true)  C(9,true,false,false,true)  C(10,false,true,false,true) C(11,true,true,false,true)
      C(12,false,false,true,true)  C(13,true,false,true,true)  C(14,false,true,true,true)  C(15,true,true,true,true)
#undef C
    }
#if PICO3D_PROF
    pico3d_prof_fill_cyc += pico3d_prof_cyc() - pf;
#endif
    return written;
  }

}
