// pico3d mesh draw stage: transform -> light -> near-cull -> rasterise.
//
// Lighting is resolved here, per vertex, and baked into the triangle's vertex
// colours so the rasteriser stays shading-mode agnostic (see pico3d_raster.cpp).
//
// Near-plane handling: a triangle wholly in front of the near plane takes the
// cached screen projection straight from the vertex cache. One that crosses it
// is cut against it and re-projected, which costs a clip and a divide per new
// vertex - so the test is on the whole triangle and the common case pays
// nothing. Without this a triangle with any vertex behind the eye had to be
// dropped whole, and geometry popped in and out as you moved through it.

#include "pico3d.hpp"

#include <algorithm>
#include <cstddef>              // offsetof
#include <cmath>

// On-device: pin the shared pass-2 loop to SRAM (both cores run it; flash/XIP would
// contend) and enable the dual-core split. No-ops on host (no pico-sdk).
#if __has_include("pico/multicore.h")
#include "pico.h"               // __not_in_flash_func
#define PICO3D_MULTICORE 1
// core1 is owned by picovector — it launches it correctly (gating MicroPython's FIFO
// IRQ) and enables core1's FPU. We BORROW it via picovector's generic dispatch; a
// second multicore_launch would hang (core1 isn't in the bootrom wait state).
extern "C" void pv_core1_run(void (*fn)());
extern "C" void pv_core1_join();
// picovector's shared scratch pool, borrowed for the per-band triangle bins. Free
// while a 3D pass runs (nothing is rasterising), and sized by the embedder, so the
// capacity is read from the pool rather than assumed.
#include "picovector_working_buffer.h"
#endif
#ifndef __not_in_flash_func
#define __not_in_flash_func(f) f
#endif

// TEMP phase profiler (cycle-accurate; see pico3d.hpp). draw_mesh times the
// vertex TRANSFORM (pass 1) + counts covered pixels; the per-triangle SETUP and
// scanline FILL are timed inside pico3d_raster_triangle. Read/zeroed by prof().
namespace picovector {

  uint64_t pico3d_prof_transform_cyc[2] = {};
  uint64_t pico3d_prof_build_cyc[2] = {};
  uint64_t pico3d_prof_project_cyc[2] = {};
  uint64_t pico3d_prof_planes_cyc[2] = {};
  uint64_t pico3d_prof_edges_cyc[2] = {};
  uint64_t pico3d_prof_fill_cyc[2] = {};
  uint64_t pico3d_prof_bbox_px[2] = {};
  uint64_t pico3d_prof_px[2] = {};
  uint32_t pico3d_prof_detail[2][PICO3D_PD_COUNT] = {};
#if defined(__arm__)
  bool pico3d_prof_enabled[2] = { false, false };
  void __attribute__((noinline)) pico3d_prof_enable(int core) {
    *(volatile uint32_t *)0xE000EDFCu |= (1u << 24);    //   DEMCR.TRCENA
    *(volatile uint32_t *)0xE0001000u |= 1u;            //   DWT_CTRL.CYCCNTENA
    pico3d_prof_enabled[core] = true;
  }
#endif

  using std::min;
  using std::max;

  static constexpr float NEAR_EPS = 1e-4f;

  // --- near-plane clip ------------------------------------------------------
  // A vertex on its way to the rasteriser, still in clip space so it can be cut
  // against the eye plane. Its varyings travel with it: whatever the shading mode
  // put in uv/rgb/n/tan, a vertex the clip invents needs the interpolated value.
  struct clipvert_t {
    vec4_t   clip;
    vec3_t   uv;
    uint32_t rgb;
    vec3_t   n, tan;
  };

  // Per-channel blend of two 0x00BBGGRR words in .8 fixed point.
  static inline uint32_t lerp_rgb(uint32_t a, uint32_t b, float t) {
    int ti = (int)(t * 256.0f);
    auto ch = [&](int shift) -> uint32_t {
      int x = (int)((a >> shift) & 0xff), y = (int)((b >> shift) & 0xff);
      int v = x + (((y - x) * ti) >> 8);
      return (uint32_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    };
    return ch(0) | (ch(8) << 8) | (ch(16) << 16);
  }

  static inline clipvert_t lerp_clipvert(const clipvert_t &a, const clipvert_t &b, float t) {
    clipvert_t r;
    r.clip = vec4_t(a.clip.x + (b.clip.x - a.clip.x) * t,
                    a.clip.y + (b.clip.y - a.clip.y) * t,
                    a.clip.z + (b.clip.z - a.clip.z) * t,
                    a.clip.w + (b.clip.w - a.clip.w) * t);
    r.uv  = a.uv.lerp(b.uv, t);
    r.rgb = lerp_rgb(a.rgb, b.rgb, t);
    r.n   = a.n.lerp(b.n, t);
    r.tan = a.tan.lerp(b.tan, t);
    return r;
  }

  // Distance from the near plane in homogeneous clip space. The projection puts
  // the near plane at z = -w, so this is positive in front of it. Clipping here
  // rather than at w > 0 is what keeps 1/w bounded by 1/near: cut at the eye
  // instead and the new vertices come back with iw in the thousands, screen
  // coordinates in the millions, and an integer edge setup that overflows into
  // the wrong winding - which culled the triangle and smeared its texture.
  static inline float near_distance(const vec4_t &c) { return c.z + c.w; }

  // 1/w for the perspective divide, exactly as the transform works it out: the
  // cache keeps w, which fog needs as well, and each path that wants the
  // reciprocal recomputes it rather than the entry carrying both.
  static inline float inv_w(float w) { return (w > NEAR_EPS) ? 1.0f / w : 0.0f; }

  // Bytes a vertex-cache entry takes for a draw: the common prefix, plus the
  // extras its shading path reads (see pico3d_vcache_t).
  // need_w: some path will read the entry's w - texture coordinates (1/w), a
  // per-pixel-lit or matcap material, or depth fog on the target at add()
  // time. Without it (and with no ext fields) an entry is two 8-byte lines,
  // [sx sy][z rgb], and the transform's PSRAM writes drop by a third. Fog
  // switched on between add() and draw() is skipped for such a submission:
  // the eye-space depth it needs was never stored.
  static inline uint32_t vcache_stride(pico3d_shading_t shading, bool do_nmap, bool do_matcap,
                                       bool need_w) {
    if (do_nmap) return (uint32_t)offsetof(pico3d_vcache_t, ext) + 2 * (uint32_t)sizeof(vec3_t);
    if (do_matcap || shading == PICO3D_FLAT)
      return (uint32_t)offsetof(pico3d_vcache_t, ext) + (uint32_t)sizeof(vec3_t);
    return need_w ? (uint32_t)offsetof(pico3d_vcache_t, ext)
                  : (uint32_t)offsetof(pico3d_vcache_t, w);
  }
  static_assert(offsetof(pico3d_vcache_t, w) == 12, "the short vcache entry is 12 bytes");
  static_assert(offsetof(pico3d_vcache_t, ext) == 20, "vcache prefix is 20 bytes");
  static_assert(sizeof(pico3d_vcache_t) == 44, "vcache entry is at most 44 bytes");

  // Does a draw to this target with this material read the entries' w?
  static inline bool vcache_need_w(const pico3d_target_t *t, const pico3d_material_t *m) {
    return (t->fog_far > t->fog_near) ||
           m->texture || m->matcap || m->normal_map || m->specular != 0;
  }

  // Entry i of a packed cache.
  static inline const pico3d_vcache_t &vcache_at(const char *vb, uint32_t vs, uint32_t i) {
    return *(const pico3d_vcache_t *)(vb + (size_t)i * vs);
  }

  // --- guard band ------------------------------------------------------------
  // The rasteriser's edge functions are exact 32-bit integers, which holds for
  // vertices within PICO3D_RASTER_LIMIT_PX of the target's origin. So anything
  // reaching past GUARD_PX is cut back to the guard band first, in clip space,
  // like the near plane. The gap between the two is for the float error on a
  // vertex the clip places exactly on a guard plane. Only very large triangles
  // close to the camera reach this far, so the clip is rare.
  static constexpr float GUARD_PX = 1000.0f;
  static_assert(GUARD_PX < (float)PICO3D_RASTER_LIMIT_PX, "guard band must sit inside the raster's range");

  static inline bool outside_guard(float sx, float sy) {
    return sx < -GUARD_PX || sx > GUARD_PX || sy < -GUARD_PX || sy > GUARD_PX;
  }

  // A vertex behind the near plane has no real projection, and one beyond the
  // guard band has no storable one: both park at the sentinel, and the one
  // screen-position test every hot path already makes sends their triangles to
  // the clipper (which recomputes from the mesh).
  static inline bool parked(int16_t sxq) { return sxq == PICO3D_PARKED_Q; }
  static inline int16_t snap_q(float s) { return (int16_t)(int32_t)(s * 16.0f); }

  // Sutherland-Hodgman against one plane, as a . (x, y, z, w) >= 0 in clip
  // space: a convex polygon of n vertices in, at most n + 1 out. Interpolating in
  // clip space - before the perspective divide - is what keeps the new vertices
  // on the original triangle's plane.
  static int clip_plane(const clipvert_t *in, int n, clipvert_t *out, const float pl[4]) {
    auto d = [&](const vec4_t &c) { return pl[0] * c.x + pl[1] * c.y + pl[2] * c.z + pl[3] * c.w; };
    int m = 0;
    for (int i = 0; i < n; i++) {
      const clipvert_t &a = in[i];
      const clipvert_t &b = in[(i + 1) % n];
      float da = d(a.clip), db = d(b.clip);
      bool a_in = da >= 0.0f, b_in = db >= 0.0f;
      if (a_in) out[m++] = a;
      if (a_in != b_in) {
        float dd = da - db;
        out[m++] = lerp_clipvert(a, b, dd != 0.0f ? da / dd : 0.0f);
      }
    }
    return m;
  }

  // Viewport map for a clipped vertex. Matches transform_range's cached
  // projection exactly, so a clipped triangle lands on the same pixels its
  // unclipped neighbours do and shared edges stay watertight.
  static inline void project_into(pico3d_tri_t &tri, int k, const clipvert_t &v,
                                  float tw, float th) {
    float w = 1.0f / v.clip.w;
    // the same 28.4 snap the transform caches, so a clipped triangle lands on
    // the same pixels its unclipped neighbours do and shared edges stay tight
    tri.sxq[k] = (int32_t)((v.clip.x * w * 0.5f + 0.5f) * tw * 16.0f);
    tri.syq[k] = (int32_t)((1.0f - (v.clip.y * w * 0.5f + 0.5f)) * th * 16.0f);
    tri.z[k]   = v.clip.z * w;
    tri.iw[k]  = w;
    tri.uv_[k] = v.uv;
    tri.rgb[k] = v.rgb;
    tri.n[k]   = v.n;
    tri.tan[k] = v.tan;
  }

  // --- pass 2: per-triangle assembly + rasterise ----------------------------
  // Factored out of pico3d_draw_mesh so it can run on EITHER core over a band of
  // the target (the band is just the target's clip_y0/clip_y1 — the rasteriser
  // already clamps each triangle's bbox to it). Reads the shared (read-only) vertex
  // cache; writes only its band of the colour/depth buffers, so two cores over
  // disjoint bands never race. Plain data in/out -> safe to call from core1.
  struct pass2_job_t {
    pico3d_target_t          target;        // a COPY; clip_y0/clip_y1 select the band
    const pico3d_mesh_t     *mesh;
    const uint16_t          *ind;           // the mesh's indices, or the scene's SRAM copy
    const char              *vc;            // packed vertex cache, vs bytes an entry
    uint32_t                 vs;
    mat4_t                   mvp;           // to re-project a triangle the near plane cuts
    const pico3d_material_t *material;
    const pico3d_light_t    *light;         // FLAT face lighting
    const pico3d_light_t    *raster_light;  // per-pixel lit path (or null)
    vec3_t                   L;
    float                    tw, th;        // target size, for re-projecting a clip
    uint32_t                 fog;           // linear depth fog (see pico3d_target_t)
    float                    fog_far, fog_scale;   // fog_scale 0 = no fog
    pico3d_shading_t         shading;
    bool                     do_nmap, do_matcap;
  };

  // A triangle that straddles the eye plane or reaches past the guard band: cut
  // it against the planes it crosses and fan what is left. The cached
  // projection is no use here, so each new vertex is projected on the spot -
  // from a clip position recomputed the way the transform made it, since the
  // cache does not keep one for the rare triangle that needs it. Rare enough
  // to live in flash, which keeps draw_pass2 small in SRAM.
  static int __attribute__((noinline)) draw_clipped(const pass2_job_t &j, const uint16_t idx[3],
                                                    const vec3_t vuv[3], const uint32_t vrgb[3],
                                                    const vec3_t vn[3], const vec3_t vtan[3]) {
    pico3d_target_t *t = (pico3d_target_t *)&j.target;
    // Each plane can add a vertex: 3 + near + 4 guard planes = 8 at most.
    clipvert_t buf[2][8];
    for (int k = 0; k < 3; k++) {
      const float *pp = j.mesh->positions + (size_t)idx[k] * 3;
      buf[0][k].clip = j.mvp * vec3_t(pp[0], pp[1], pp[2]);
      buf[0][k].uv = vuv[k]; buf[0][k].rgb = vrgb[k];
      buf[0][k].n = j.do_nmap ? vn[k] : vec3_t(0, 0, 0);
      buf[0][k].tan = j.do_nmap ? vtan[k] : vec3_t(0, 0, 0);
    }
    // The guard band in NDC, from its extent in pixels: sx = (x/w * 0.5 + 0.5) * tw
    // and sy = (0.5 - y/w * 0.5) * th, solved for x/w and y/w at sx, sy = +-GUARD_PX.
    const float gx = 2.0f * GUARD_PX / j.tw, gy = 2.0f * GUARD_PX / j.th;
    const float planes[5][4] = {
      { 0.0f,  0.0f, 1.0f, 1.0f },          // near: z + w >= 0 (near_distance)
      { 1.0f,  0.0f, 0.0f, gx + 1.0f },     // sx >= -GUARD_PX
      {-1.0f,  0.0f, 0.0f, gx - 1.0f },     // sx <=  GUARD_PX
      { 0.0f, -1.0f, 0.0f, gy + 1.0f },     // sy >= -GUARD_PX
      { 0.0f,  1.0f, 0.0f, gy - 1.0f },     // sy <=  GUARD_PX
    };
    int n = 3, cur = 0;
    for (const auto &pl : planes) {
      // skip a plane every vertex is already inside, so the usual case is one pass
      bool all_in = true;
      for (int k = 0; k < n && all_in; k++) {
        const vec4_t &c = buf[cur][k].clip;
        all_in = pl[0] * c.x + pl[1] * c.y + pl[2] * c.z + pl[3] * c.w >= 0.0f;
      }
      if (all_in) continue;
      n = clip_plane(buf[cur], n, buf[cur ^ 1], pl);
      cur ^= 1;
      if (n < 3) return 0;
    }
    int drawn = 0;
    const clipvert_t *out = buf[cur];
    for (int k = 1; k + 1 < n; k++) {
      pico3d_tri_t tri{};
      project_into(tri, 0, out[0], j.tw, j.th);
      project_into(tri, 1, out[k], j.tw, j.th);
      project_into(tri, 2, out[k + 1], j.tw, j.th);
      int wrote = pico3d_raster_triangle(t, &tri, j.material, j.raster_light);
      pico3d_prof_px[PICO3D_PC] += (uint64_t)wrote;
      if (wrote > 0) drawn++;
    }
    return drawn;
  }

  // `bin` (or null = all triangles): a list of triangle indices this core should fill —
  // pre-binned to its band so each core only SETS UP the triangles in its half, instead
  // of every core setting up every triangle. That's how the per-triangle setup parallelises.
  static int __not_in_flash_func(draw_pass2)(const pass2_job_t &j, const uint16_t *bin, uint32_t bincount) {
    pico3d_target_t *t = (pico3d_target_t *)&j.target;
    const pico3d_mesh_t *mesh = j.mesh;
    const char *vb = j.vc; const uint32_t vs = j.vs;
    auto V = [&](uint32_t i) -> const pico3d_vcache_t & { return vcache_at(vb, vs, i); };
    bool do_nmap = j.do_nmap, do_matcap = j.do_matcap;
    pico3d_shading_t shading = j.shading;
    vec3_t L = j.L;
    auto uv = [&](uint32_t i) {
      return mesh->uvs ? vec3_t(mesh->uvs[i*2], mesh->uvs[i*2+1], 0.0f) : vec3_t(0,0,0);
    };
    uint32_t n = bin ? bincount : mesh->triangle_count;
    int drawn = 0;
    // The rasteriser reads iw and uv_ only to interpolate texture coordinates,
    // so a material with nothing to sample leaves both unwritten (and the
    // entry's [w nd] cache line unread). The rasteriser only looks at them on
    // its textured or per-pixel-lit paths, which such a material never takes.
    const pico3d_material_t *mt = j.material;
    const bool need_uv = mt->texture || mt->matcap || mt->normal_map || mt->specular || do_nmap || do_matcap;
    PICO3D_PD_START(pd_p2);
    PICO3D_PD_COUNTN(PICO3D_PD_TRIS_IN, n);
    for (uint32_t bi = 0; bi < n; bi++) {
      uint32_t f = bin ? bin[bi] : bi;
      uint16_t i0 = j.ind[f*3], i1 = j.ind[f*3+1], i2 = j.ind[f*3+2];
      const uint16_t idx[3] = {i0, i1, i2};

      // The varyings, resolved once per vertex whichever path takes them. FLAT
      // takes its face normal from the unclipped triangle, so the light value is
      // the same for every piece a clip leaves behind.
      vec3_t vuv[3], vn[3], vtan[3];
      uint32_t vrgb[3];
      if (need_uv) { vuv[0] = uv(i0); vuv[1] = uv(i1); vuv[2] = uv(i2); }
      if (do_nmap) {
        for (int k = 0; k < 3; k++) {
          vrgb[k] = V(idx[k]).rgb; vn[k] = V(idx[k]).ext[0]; vtan[k] = V(idx[k]).ext[1];
        }
      } else if (do_matcap) {
        for (int k = 0; k < 3; k++) { vrgb[k] = V(idx[k]).rgb; vuv[k] = V(idx[k]).ext[0]; }
      } else if (shading == PICO3D_FLAT) {
        vec3_t n = (V(i1).ext[0] - V(i0).ext[0]).cross(V(i2).ext[0] - V(i0).ext[0]).normalized();
        uint32_t lv = pico3d_light_value(j.light, n.dot(L));
        for (int k = 0; k < 3; k++) vrgb[k] = pico3d_modulate(V(idx[k]).rgb, lv);
      } else {                                          // UNLIT / GOURAUD pre-baked
        for (int k = 0; k < 3; k++) vrgb[k] = V(idx[k]).rgb;
      }

      // Depth fog, after the light so it cannot pick up the surface's facing.
      // w is the eye-space depth the projection already worked out, so this
      // costs a subtract, a clamp and a mix per vertex.
      if (j.fog_scale != 0.0f && j.vs > (uint32_t)offsetof(pico3d_vcache_t, w)) {
        for (int k = 0; k < 3; k++) {
          float f = (j.fog_far - V(idx[k]).w) * j.fog_scale;
          f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
          vrgb[k] = lerp_rgb(j.fog, vrgb[k], f);
        }
      }

      // (a vertex behind the near plane is parked, as is one beyond the guard)
      if (!parked(V(i0).sxq) && !parked(V(i1).sxq) && !parked(V(i2).sxq)) {
        // Deliberately not value-initialised: it is 168 bytes, and zeroing it
        // per triangle cost more than some triangles' fill. Every field a path
        // reads is written on that path: sx/sy/z/rgb always, iw and uv_ for
        // materials with something to sample, n/tan when normal-mapped.
        pico3d_tri_t tri;
        for (int k = 0; k < 3; k++) {                   // copy the CACHED screen projection
          const pico3d_vcache_t &e = V(idx[k]);
          tri.sxq[k] = e.sxq; tri.syq[k] = e.syq;
          tri.z[k]  = e.z; tri.rgb[k] = vrgb[k];
          if (need_uv) { tri.iw[k] = inv_w(e.w); tri.uv_[k] = vuv[k]; }
        }
        if (do_nmap) for (int k = 0; k < 3; k++) { tri.n[k] = vn[k]; tri.tan[k] = vtan[k]; }
        PICO3D_PD_START(pd_r);
        int wrote = pico3d_raster_triangle(t, &tri, j.material, j.raster_light);
        PICO3D_PD_ADD(PICO3D_PD_RASTER, pd_r);
        pico3d_prof_px[PICO3D_PC] += (uint64_t)wrote;   // the raster already counts these
        if (wrote > 0) drawn++;
        continue;
      }

      // Straddles the eye plane or the guard band: rare, so cut and fanned out
      // of line (flash).
      PICO3D_PD_COUNTN(PICO3D_PD_TRIS_CLIPPED, 1);
      drawn += draw_clipped(j, idx, vuv, vrgb, vn, vtan);
    }
    PICO3D_PD_COUNTN(PICO3D_PD_TRIS_DRAWN, drawn);
    PICO3D_PD_ADD(PICO3D_PD_PASS2, pd_p2);
    return drawn;
  }

  // --- core split: run pass 2 over the whole target, or top/bottom on 2 cores -
  static int g_cores = 1;                                // 1 = this core only, 2 = both

#if PICO3D_MULTICORE
  static volatile const pass2_job_t *g_job1;             // the bottom-band job for core1
  static const uint16_t * volatile g_bin1;               // its triangle bin
  static volatile uint32_t g_bincount1;
  static volatile int g_drawn1;
  static void __not_in_flash_func(pico3d_core1_fn)() {   // runs ON core1 via picovector
    g_drawn1 = draw_pass2(*(const pass2_job_t *)g_job1, (const uint16_t *)g_bin1, g_bincount1);
  }
#endif

  static int pico3d_dispatch_pass2(pass2_job_t &job) {
#if PICO3D_MULTICORE
    // The bins share the working buffer with whatever depth buffer the embedder
    // may have put there, so they start after it and get only what is left.
    char *pool = PicoVector_working_buffer;
    size_t pool_size = working_buffer_size;
    const char *depth = (const char *)job.target.depth;
    if (depth >= pool && depth < pool + pool_size) {
      size_t used = (size_t)(depth - pool) +
                    (size_t)job.target.depth_stride * (size_t)job.target.height * sizeof(uint16_t);
      used = (used + 3) & ~(size_t)3;
      pool += used < pool_size ? used : pool_size;
      pool_size -= used < pool_size ? used : pool_size;
    }
    const uint32_t bin_cap = (uint32_t)(pool_size / sizeof(uint16_t)) / 2;
    if (g_cores == 2 && job.mesh->triangle_count >= 8 &&
        job.mesh->triangle_count <= bin_cap) {
      int y0 = job.target.clip_y0, y1 = job.target.clip_y1;
      // Split at the MESH's on-screen vertical midpoint (not the screen's), then BIN each
      // triangle into the top and/or bottom half by its cached screen-Y bbox. Each core
      // then only sets up + fills the triangles in ITS band — so the per-triangle setup
      // is split between the cores instead of duplicated (only band-straddling triangles
      // are set up twice). Bins live in picovector's working buffer.
      auto V = [&](uint32_t i) -> const pico3d_vcache_t & { return vcache_at(job.vc, job.vs, i); };
      int32_t mny = INT32_MAX, mxy = INT32_MIN;
      for (uint32_t v = 0, nv = job.mesh->vertex_count; v < nv; v++) {
        // Only to pick where to split the screen, so a vertex with no meaningful
        // projection is simply left out of the extent.
        if (parked(V(v).sxq)) continue;
        int32_t sy = V(v).syq;
        if (sy < mny) mny = sy;
        if (sy > mxy) mxy = sy;
      }
      if (mxy > mny) {
        int mid = (int)((mny + mxy) / 32);               // mean of two 28.4 rows, in pixels
        if (mid < y0) mid = y0; else if (mid > y1) mid = y1;
        const int32_t midq = mid * 16;
        uint16_t *top_bin = (uint16_t *)pool;
        uint16_t *bot_bin = top_bin + bin_cap;
        uint32_t nt = 0, nb = 0;
        const uint16_t *ind = job.ind;
        for (uint32_t f = 0, T = job.mesh->triangle_count; f < T; f++) {
          uint16_t a = ind[f*3], b = ind[f*3+1], c = ind[f*3+2];
          if (parked(V(a).sxq) || parked(V(b).sxq) || parked(V(c).sxq)) {
            // Crosses the near plane, so its cached sy is meaningless and there
            // is no telling which band the clipped pieces land in. Bin it to
            // both and let each core clip it against its own rows.
            top_bin[nt++] = (uint16_t)f;
            bot_bin[nb++] = (uint16_t)f;
            continue;
          }
          int32_t lo = V(a).syq, hi = lo, sv;
          sv = V(b).syq; if (sv < lo) lo = sv; else if (sv > hi) hi = sv;
          sv = V(c).syq; if (sv < lo) lo = sv; else if (sv > hi) hi = sv;
          if (lo <  midq) top_bin[nt++] = (uint16_t)f;
          if (hi >= midq) bot_bin[nb++] = (uint16_t)f;
        }
        pass2_job_t top = job, bot = job;
        top.target.clip_y1 = mid;                        // core0: rows [y0, mid)
        bot.target.clip_y0 = mid;                        // core1: rows [mid, y1)
        g_job1 = &bot; g_bin1 = bot_bin; g_bincount1 = nb;
        __sync_synchronize();                            // publish vcache + bins to core1
        ::pv_core1_run(pico3d_core1_fn);                 // core1: bottom band, its bin only
        int d0 = draw_pass2(top, top_bin, nt);           // core0: top band, its bin only
        ::pv_core1_join();
        return d0 + g_drawn1;
      }
    }
#endif
    return draw_pass2(job, nullptr, 0);
  }

  void pico3d_set_cores(int n) { g_cores = (n >= 2) ? 2 : 1; }
  int  pico3d_get_cores() { return g_cores; }
  static int g_work_bands = 4;
  void pico3d_set_work_bands(int n) { g_work_bands = n < 1 ? 1 : (n > 64 ? 64 : n); }
  int  pico3d_get_work_bands() { return g_work_bands; }

  // --- pass 1: transform (+ light) a VERTEX RANGE — factored so it can split across
  // cores. Each vertex writes its own vcache slot, so two cores over disjoint ranges
  // never race. (Embarrassingly parallel; this is the win for transform-bound meshes.)
  struct xform_job_t {
    const pico3d_mesh_t     *mesh;   char *vc; uint32_t vs;   // packed cache, vs bytes an entry
    const pico3d_material_t *material; const pico3d_light_t *light;
    const mat4_t            *model;  mat4_t mvp, mc_nrm;  vec3_t L;
    float                    tw, th;  // target width/height (for the cached screen projection)
    pico3d_shading_t         shading; bool do_nmap, has_nmap, do_matcap;
  };

  static void __not_in_flash_func(transform_range)(const xform_job_t &j, uint32_t v0, uint32_t v1) {
    const pico3d_mesh_t *mesh = j.mesh;
    const pico3d_material_t *material = j.material; const pico3d_light_t *light = j.light;
    const mat4_t *model = j.model; const mat4_t &mvp = j.mvp; const mat4_t &mc_nrm = j.mc_nrm;
    bool do_nmap = j.do_nmap, has_nmap = j.has_nmap, do_matcap = j.do_matcap;
    pico3d_shading_t shading = j.shading; vec3_t L = j.L;
    auto nrm = [&](uint32_t i){ return vec3_t(mesh->normals[i*3], mesh->normals[i*3+1], mesh->normals[i*3+2]); };
    auto tan = [&](uint32_t i){ return vec3_t(mesh->tangents[i*3], mesh->tangents[i*3+1], mesh->tangents[i*3+2]); };
    PICO3D_PD_START(pd_x);
    for (uint32_t v = v0; v < v1; v++) {
      vec3_t p(mesh->positions[v*3], mesh->positions[v*3+1], mesh->positions[v*3+2]);
      pico3d_vcache_t &o = *(pico3d_vcache_t *)(j.vc + (size_t)v * j.vs);
      vec4_t c = mvp * p;
      float w = inv_w(c.w);                              // pre-project to screen, once
      float nd = near_distance(c);
      if (nd < 0.0f) {                                   // behind the near plane: no projection
        o.sxq = o.syq = PICO3D_PARKED_Q;
      } else {
        float sx = (c.x * w * 0.5f + 0.5f) * j.tw;
        float sy = (1.0f - (c.y * w * 0.5f + 0.5f)) * j.th;
        if (outside_guard(sx, sy)) {                     // clipper recomputes from the mesh
          o.sxq = o.syq = PICO3D_PARKED_Q;
        } else {
          o.sxq = snap_q(sx); o.syq = snap_q(sy);        // the raster's own 28.4 snap, once
        }
      }
      o.z  = c.z * w;
      // a short entry ends at z/rgb: nothing will read w or nd (see vcache_stride)
      if (j.vs > (uint32_t)offsetof(pico3d_vcache_t, w)) { o.w = c.w; o.nd = nd; }
      uint32_t b = mesh->colors ? mesh->colors[v] : material->color;
      if (do_nmap) {
        o.rgb = b;
        o.ext[0] = model->transform_direction(nrm(v)).normalized();
        o.ext[1] = has_nmap ? model->transform_direction(tan(v)).normalized() : vec3_t(0, 0, 0);
      } else if (do_matcap) {
        vec3_t n = mc_nrm.transform_direction(nrm(v)).normalized();
        o.rgb = b;
        o.ext[0] = vec3_t(n.x * 0.5f + 0.5f, 0.5f - n.y * 0.5f, 0.0f);
      } else if (shading == PICO3D_GOURAUD) {
        vec3_t n = model->transform_direction(nrm(v)).normalized();
        uint32_t lv;
        if (light->point) {
          vec3_t Lv = light->position - (*model * p).xyz();
          float d2 = Lv.dot(Lv); if (d2 < 1e-8f) d2 = 1e-8f;
          lv = pico3d_light_value(light, n.dot(Lv * (1.0f / sqrtf(d2))), 1.0f / (1.0f + light->atten * d2));
        } else {
          lv = pico3d_light_value(light, n.dot(L));
        }
        o.rgb = pico3d_modulate(b, lv);
      } else if (shading == PICO3D_FLAT) {
        o.ext[0] = (*model * p).xyz();
        o.rgb = b;
      } else {
        o.rgb = b;
      }
    }
    PICO3D_PD_ADD(PICO3D_PD_XFORM, pd_x);
    PICO3D_PD_COUNTN(PICO3D_PD_VERTS, v1 - v0);
  }

#if PICO3D_MULTICORE
  static const xform_job_t * volatile g_xj;              // vertex-transform job for core1
  static volatile uint32_t g_xv0, g_xv1;                 // core1's vertex range
  static void __not_in_flash_func(pico3d_core1_xform)() {
    transform_range(*g_xj, g_xv0, g_xv1);
  }
#endif

  // --- the triangle pass of scene_add: row extents and early culls ----------
  // This is what lets a band skip a triangle for the price of two compares
  // instead of a full per-triangle setup, and it is why the extents live in
  // their own tight array rather than inside the vertex cache: a band scans
  // them start to end and touches nothing else.
  //
  // A triangle that can never write a pixel gets an empty extent, so no band
  // bins it: back-facing (the raster's own test, on the same cached values in
  // the same order, so exactly the triangles it would refuse), zero-area, or
  // wholly left or right of the target. Otherwise every band it touches would
  // assemble it - uvs, colours, fog - only for the raster to throw it away.
  struct extents_job_t {
    const char *vb; uint32_t vs;        // the mesh's packed vertex cache
    const uint16_t *ind;
    int16_t *ys;                        // two per triangle: first and last row
    bool cull_back;
    float tw;
  };

  // Triangles [f0, f1), reporting the rows the visible ones touch. Pure
  // integer: the cached 28.4 positions are the same values the raster snaps
  // to, so the backface sign here agrees with the fill exactly.
  static void __not_in_flash_func(extents_range)(const extents_job_t &j, uint32_t f0, uint32_t f1,
                                                 int32_t &ymin_out, int32_t &ymax_out,
                                                 int32_t &xmin_out, int32_t &xmax_out) {
    PICO3D_PD_START(pd_ext);
    auto V = [&](uint32_t i) -> const pico3d_vcache_t & { return vcache_at(j.vb, j.vs, i); };
    const uint16_t *ind = j.ind;
    int16_t *ys = j.ys;
    const bool cull_back = j.cull_back;
    const int32_t twq = (int32_t)j.tw * 16;
    int32_t ymin = INT32_MAX, ymax = INT32_MIN;
    int32_t xmin = INT32_MAX, xmax = INT32_MIN;
    for (uint32_t f = f0; f < f1; f++) {
      uint16_t a = ind[f*3], b = ind[f*3+1], c = ind[f*3+2];
      const pico3d_vcache_t &va = V(a), &vb_ = V(b), &vc_ = V(c);
      if (parked(va.sxq) || parked(vb_.sxq) || parked(vc_.sxq)) {
        // Behind the near plane or beyond the guard band: the cached snap means
        // nothing and there is no telling which rows the clipped pieces land
        // in. Claim every band and let each one clip it.
        ys[f*2] = -32768; ys[f*2+1] = 32767;
        ymin = -32768; ymax = 32767;
        xmin = -32768; xmax = 32767;
        continue;
      }
      const int32_t sx0 = va.sxq, sx1 = vb_.sxq, sx2 = vc_.sxq;   // 28.4
      const int32_t sy0 = va.syq, sy1 = vb_.syq, sy2 = vc_.syq;
      // In-guard coordinates are at most +/-16000, so each product is under
      // 2^30 and the difference fits 32 bits - the raster's own bound.
      const int32_t denom = (sx1 - sx0) * (sy2 - sy0) - (sy1 - sy0) * (sx2 - sx0);
      int32_t xlo = sx0 < sx1 ? sx0 : sx1; if (sx2 < xlo) xlo = sx2;
      int32_t xhi = sx0 > sx1 ? sx0 : sx1; if (sx2 > xhi) xhi = sx2;
      if (denom == 0 || (cull_back && denom > 0) || xhi < 0 || xlo >= twq) {
        ys[f*2] = 32767; ys[f*2+1] = -32768;        // touches no band
        continue;
      }
      int32_t ylo = sy0 < sy1 ? sy0 : sy1; if (sy2 < ylo) ylo = sy2;
      int32_t yhi = sy0 > sy1 ? sy0 : sy1; if (sy2 > yhi) yhi = sy2;
      // Sub-pixel cull, directly on the snapped values the rasteriser will
      // test: is there a pixel centre (k * 16 + 8) inside the min and max?
      if (((xlo - 8 + 15) >> 4) > ((xhi - 8) >> 4) ||
          ((ylo - 8 + 15) >> 4) > ((yhi - 8) >> 4)) {
        ys[f*2] = 32767; ys[f*2+1] = -32768;        // spans no pixel centre
        continue;
      }
      int ilo = ylo / 16, ihi = yhi / 16 + 1;       // +1: round the far edge out
      ys[f*2]   = (int16_t)ilo;
      ys[f*2+1] = (int16_t)ihi;
      if (ilo < ymin) ymin = ilo;
      if (ihi > ymax) ymax = ihi;
      // Columns are not stored per triangle - the bands select by row - they
      // only bound the scene so a draw can narrow its clip (and depth clears).
      const int ixlo = xlo / 16, ixhi = xhi / 16 + 1;
      if (ixlo < xmin) xmin = ixlo;
      if (ixhi > xmax) xmax = ixhi;
    }
    ymin_out = ymin; ymax_out = ymax;
    xmin_out = xmin; xmax_out = xmax;
    PICO3D_PD_ADD(PICO3D_PD_EXTENTS, pd_ext);
  }

#if PICO3D_MULTICORE
  static const extents_job_t * volatile g_ej;           // extents job for core1
  static volatile uint32_t g_ef0, g_ef1;                 // core1's triangle range
  static volatile int32_t g_eymin, g_eymax;              // and the rows it found
  static volatile int32_t g_exmin, g_exmax;              // ... and the columns
  static void __not_in_flash_func(pico3d_core1_extents)() {
    int32_t lo, hi, xlo, xhi;
    extents_range(*g_ej, g_ef0, g_ef1, lo, hi, xlo, xhi);
    g_eymin = lo; g_eymax = hi; g_exmin = xlo; g_exmax = xhi;
  }
#endif

  // What a draw resolves before it can transform anything: which shading path
  // applies, the matrices the vertex stage needs, and the per-draw light copy.
  // Both the immediate path (pico3d_draw_mesh) and the deferred one
  // (pico3d_scene_add) start here, so they cannot drift apart.
  namespace {
    struct prepared_t {
      mat4_t           mvp, mc_nrm;
      pico3d_light_t   rlight;
      bool             raster_lit;      // rlight is live: lighting is per pixel
      vec3_t           L;
      pico3d_shading_t shading;
      bool             do_nmap, has_nmap, do_matcap;
    };

    static prepared_t prepare_draw(const pico3d_mesh_t *mesh, const mat4_t *model,
                                   const mat4_t *view_proj,
                                   const pico3d_material_t *material,
                                   pico3d_shading_t shading,
                                   const pico3d_light_t *light, const mat4_t *view) {
      prepared_t p{};
      p.mvp = (*view_proj) * (*model);

      // Normal mapping needs a map, a light, and per-vertex normals+tangents. When
      // active it overrides the shading mode (lighting is done per-pixel in the
      // rasteriser); the rasteriser only treats this triangle as normal-mapped when
      // we pass it `light` (raster_lit below), so the gate is exact.
      // Per-pixel lit path: a normal map (needs tangents) OR Blinn-Phong specular (needs
      // the per-pixel normal). Both light per pixel in the rasteriser.
      p.has_nmap = material->normal_map && mesh->tangents;
      bool has_spec = material->specular != 0;
      p.do_nmap = (p.has_nmap || has_spec) && light && mesh->normals;
      // Per-draw light copy so we can stash the specular half-vector (H = normalise(L+V),
      // V = the world direction toward the camera = the view matrix's row 2).
      p.rlight = light ? *light : pico3d_light_t{};
      if (has_spec && view && light) {
        vec3_t Lh = (-light->direction).normalized();
        p.rlight.half = (Lh + vec3_t(view->v20, view->v21, view->v22)).normalized();
      }
      p.raster_lit = p.do_nmap;

      // Matcap needs a map and per-vertex normals; normal_map wins if both are set.
      // Normals go to VIEW space (so the reflection tracks the camera) when a view
      // matrix is supplied, else they stay in world space.
      p.do_matcap = !p.do_nmap && material->matcap && mesh->normals;
      p.mc_nrm = p.do_matcap ? (view ? (*view) * (*model) : *model) : mat4_t();

      // Gouraud needs per-vertex normals; without them, fall back to flat.
      p.shading = shading;
      if (p.shading == PICO3D_GOURAUD && !mesh->normals) p.shading = PICO3D_FLAT;
      if (!light) p.shading = PICO3D_UNLIT;

      p.L = light ? (-light->direction).normalized() : vec3_t(0, 0, 0);
      return p;
    }
  }

  int pico3d_draw_mesh(pico3d_target_t *t, const pico3d_mesh_t *mesh,
                    const mat4_t *model, const mat4_t *view_proj,
                    const pico3d_material_t *material, pico3d_shading_t shading,
                    const pico3d_light_t *light, pico3d_vcache_t *vc,
                    const mat4_t *view) {
    prepared_t p = prepare_draw(mesh, model, view_proj, material, shading, light, view);
    if (pico3d_cull_mesh(mesh, &p.mvp)) return 0;   // wholly outside the frustum
    const mat4_t &mvp = p.mvp;
    const mat4_t &mc_nrm = p.mc_nrm;
    pico3d_light_t rlight = p.rlight;
    const pico3d_light_t *raster_light = p.raster_lit ? &rlight : nullptr;
    bool do_nmap = p.do_nmap, has_nmap = p.has_nmap, do_matcap = p.do_matcap;
    shading = p.shading;

#if PICO3D_PROF
    uint32_t c0 = pico3d_prof_cyc();
#endif
    // --- pass 1: transform + light every vertex — split across both cores in 2-core --
    vec3_t L = p.L;
    xform_job_t xj;
    const uint32_t vs = vcache_stride(shading, do_nmap, do_matcap,
                                      vcache_need_w(t, material));
    xj.mesh = mesh;       xj.vc = (char *)vc;     xj.vs = vs;
    xj.material = material; xj.light = light;
    xj.model = model;     xj.mvp = mvp;           xj.mc_nrm = mc_nrm;     xj.L = L;
    xj.tw = (float)t->width; xj.th = (float)t->height;
    xj.shading = shading; xj.do_nmap = do_nmap;   xj.has_nmap = has_nmap; xj.do_matcap = do_matcap;
#if PICO3D_MULTICORE
    if (g_cores == 2 && mesh->vertex_count >= 64) {
      uint32_t half = mesh->vertex_count >> 1;
      g_xj = &xj; g_xv0 = half; g_xv1 = mesh->vertex_count;
      __sync_synchronize();
      ::pv_core1_run(pico3d_core1_xform);                 // core1: verts [half, V)
      transform_range(xj, 0, half);                       // core0: verts [0, half)
      { PICO3D_PD_START(pd_w); ::pv_core1_join(); PICO3D_PD_ADD(PICO3D_PD_WAIT, pd_w); }
    } else
#endif
      transform_range(xj, 0, mesh->vertex_count);

#if PICO3D_PROF
    pico3d_prof_transform_cyc[PICO3D_PC] += pico3d_prof_cyc() - c0;
#endif
    // --- pass 2: assemble + rasterise (optionally split across both cores) -----
    pass2_job_t job;
    job.target = *t;          job.mesh = mesh;     job.vc = (const char *)vc;
    job.ind = mesh->indices;
    job.vs = vs;               job.mvp = mvp;
    job.material = material;   job.light = light;   job.raster_light = raster_light;
    job.L = L;                 job.shading = shading;
    job.tw = (float)t->width;  job.th = (float)t->height;
    job.fog = t->fog;          job.fog_far = t->fog_far;
    job.fog_scale = (t->fog_far > t->fog_near) ? 1.0f / (t->fog_far - t->fog_near) : 0.0f;
    job.do_nmap = do_nmap;     job.do_matcap = do_matcap;
    return pico3d_dispatch_pass2(job);
  }

  // ---- whole-mesh frustum culling ------------------------------------------

  void pico3d_mesh_bounds(pico3d_mesh_t *mesh) {
    mesh->has_bounds = 0;
    if (!mesh->positions || mesh->vertex_count == 0) return;
    const float *p = mesh->positions;
    float lo[3] = { p[0], p[1], p[2] }, hi[3] = { p[0], p[1], p[2] };
    for (uint32_t v = 1; v < mesh->vertex_count; v++) {
      for (int k = 0; k < 3; k++) {
        float c = p[v * 3 + k];
        if (c < lo[k]) lo[k] = c; else if (c > hi[k]) hi[k] = c;
      }
    }
    for (int k = 0; k < 3; k++) { mesh->bmin[k] = lo[k]; mesh->bmax[k] = hi[k]; }
    mesh->has_bounds = 1;
  }

  bool pico3d_cull_mesh(const pico3d_mesh_t *mesh, const mat4_t *m) {
    if (!mesh->has_bounds) return false;          // unknown bounds: never cull
    // Box as centre + (non-negative) half-extent, which is what the plane test
    // wants: the corner furthest along a plane normal is centre + extent.|n|.
    const float cx = 0.5f * (mesh->bmin[0] + mesh->bmax[0]);
    const float cy = 0.5f * (mesh->bmin[1] + mesh->bmax[1]);
    const float cz = 0.5f * (mesh->bmin[2] + mesh->bmax[2]);
    const float ex = 0.5f * (mesh->bmax[0] - mesh->bmin[0]);
    const float ey = 0.5f * (mesh->bmax[1] - mesh->bmin[1]);
    const float ez = 0.5f * (mesh->bmax[2] - mesh->bmin[2]);

    // The clip-space conditions are -w <= x,y,z <= w, and each is one plane in
    // the space the matrix maps FROM. Row 3 is w, rows 0..2 are x, y, z; the
    // sums and differences below are those six inequalities rearranged to
    // "plane . p >= 0 means inside". Near is z + w, matching near_distance().
    const float planes[6][4] = {
      { m->v00 + m->v30, m->v01 + m->v31, m->v02 + m->v32, m->v03 + m->v33 },  // left
      { m->v30 - m->v00, m->v31 - m->v01, m->v32 - m->v02, m->v33 - m->v03 },  // right
      { m->v10 + m->v30, m->v11 + m->v31, m->v12 + m->v32, m->v13 + m->v33 },  // bottom
      { m->v30 - m->v10, m->v31 - m->v11, m->v32 - m->v12, m->v33 - m->v13 },  // top
      { m->v20 + m->v30, m->v21 + m->v31, m->v22 + m->v32, m->v23 + m->v33 },  // near
      { m->v30 - m->v20, m->v31 - m->v21, m->v32 - m->v22, m->v33 - m->v23 },  // far
    };
    for (int i = 0; i < 6; i++) {
      const float a = planes[i][0], b = planes[i][1], c = planes[i][2], d = planes[i][3];
      // Signed distance of the box corner FURTHEST inside this plane. If even
      // that is outside, every corner is, and the mesh cannot be visible.
      const float reach = (a < 0 ? -a : a) * ex + (b < 0 ? -b : b) * ey
                        + (c < 0 ? -c : c) * ez;
      if (a * cx + b * cy + c * cz + d + reach < 0.0f) return true;
    }
    return false;
  }

  // ---- deferred scene ------------------------------------------------------
  // See the header for why this exists. In short: a depth buffer wants to be in
  // the fastest memory there is, that memory is usually far too small for a
  // whole screen, and the way round it is to depth-test one BAND of rows at a
  // time - which means every mesh has to be transformed before any band is
  // rasterised.

  void pico3d_scene_reset(pico3d_scene_t *sc) {
    sc->sub_count = 0;
    sc->vert_count = 0;
    sc->vert_bytes = 0;
    sc->tri_count = 0;
    sc->ymin = INT32_MAX;                       // empty: no rows touched
    sc->ymax = INT32_MIN;
    sc->xmin = INT32_MAX;
    sc->xmax = INT32_MIN;
  }

  bool pico3d_scene_add(pico3d_scene_t *sc, const pico3d_target_t *t,
                        const pico3d_mesh_t *mesh, const mat4_t *model,
                        const mat4_t *view_proj, const pico3d_material_t *material,
                        pico3d_shading_t shading, const pico3d_light_t *light,
                        const mat4_t *view, const char **why, bool flat_depth) {
    if (why) *why = nullptr;
    // Cull FIRST, before even the capacity checks: a mesh outside the frustum
    // takes no room in the scene, so a full scene should still swallow one
    // rather than report failure. This is the cheapest work in the pipeline and
    // it removes the most - ~60 operations against 560 cycles a vertex to
    // transform geometry that was never going to be seen. Reported as success:
    // nothing failed, there was simply nothing to add.
    PICO3D_PD_START(pd_add);
    prepared_t p = prepare_draw(mesh, model, view_proj, material, shading, light, view);
    if (pico3d_cull_mesh(mesh, &p.mvp)) { PICO3D_PD_ADD(PICO3D_PD_ADD_WALL, pd_add); return true; }

    // Nothing here allocates from a heap: a full scene is refused so a frame
    // can never stall on one. bin holds one submission's live triangles, so it
    // has to fit the largest mesh rather than the whole scene.
    if (sc->sub_count >= sc->sub_cap) { if (why) *why = "meshes"; return false; }
    // Entries pack to what the material needs, so room is counted in bytes.
    // An embedder-set region (an SRAM pool) is bounded by its real size, less
    // whatever the index cache has taken of its tail; the heap arena keeps the
    // historic bound of vert_cap worst-case entries.
    const uint32_t vs = vcache_stride(p.shading, p.do_nmap, p.do_matcap,
                                      vcache_need_w(t, material));
    const size_t vert_bound = sc->vert_arena_bytes
        ? (size_t)(sc->cache_low ? sc->cache_low : sc->vert_arena_bytes)
        : (size_t)sc->vert_cap * sizeof(pico3d_vcache_t);
    if (sc->vert_count + mesh->vertex_count > sc->vert_cap) { if (why) *why = "vertices"; return false; }
    if ((size_t)sc->vert_bytes + (size_t)mesh->vertex_count * vs > vert_bound) {
      if (why) *why = "vertex arena";
      return false;
    }
    if (sc->tri_count + mesh->triangle_count > sc->tri_cap) { if (why) *why = "triangles"; return false; }
    if (mesh->triangle_count > sc->bin_cap) { if (why) *why = "bin"; return false; }

    // The bands read this mesh's indices once per band; when the arena has an
    // SRAM tail free, cache a copy there the first time the mesh appears and
    // read fast memory every frame after. The copy is not re-checked: a mesh
    // whose *index* buffer is rewritten keeps drawing the cached list until a
    // new scene is built (positions stay live as ever).
    const uint16_t *indices = mesh->indices;
    if (sc->vert_arena_bytes) {
      if (!sc->cache_low) sc->cache_low = sc->vert_arena_bytes;
      bool hit = false;
      for (uint32_t i = 0; i < sc->cache_count; i++) {
        if (sc->cached[i].mesh == mesh) { indices = sc->cached[i].copy; hit = true; break; }
      }
      if (!hit && sc->cache_count < (uint32_t)(sizeof(sc->cached) / sizeof(sc->cached[0]))) {
        size_t bytes = ((size_t)mesh->triangle_count * 6 + 7) & ~(size_t)7;
        if (sc->cache_low >= sc->vert_bytes + bytes &&
            (size_t)sc->cache_low - bytes >= (size_t)sc->vert_bytes) {
          sc->cache_low -= (uint32_t)bytes;
          uint16_t *copy = (uint16_t *)((char *)sc->verts + sc->cache_low);
          const uint16_t *src = mesh->indices;
          for (uint32_t k = 0, n = mesh->triangle_count * 3; k < n; k++) copy[k] = src[k];
          sc->cached[sc->cache_count].mesh = mesh;
          sc->cached[sc->cache_count].copy = copy;
          sc->cache_count++;
          indices = copy;
        }
      }
    }

    char *vb = (char *)sc->verts + sc->vert_bytes;
    sc->tw = (float)t->width;
    sc->th = (float)t->height;

    // --- pass 1, exactly as the immediate path runs it ------------------------
#if PICO3D_PROF
    uint32_t c0 = pico3d_prof_cyc();
#endif
    xform_job_t xj;
    xj.mesh = mesh;         xj.vc = vb;           xj.vs = vs;
    xj.material = material;
    xj.light = light;       xj.model = model;     xj.mvp = p.mvp;
    xj.mc_nrm = p.mc_nrm;   xj.L = p.L;           xj.tw = sc->tw;
    xj.th = sc->th;         xj.shading = p.shading;
    xj.do_nmap = p.do_nmap; xj.has_nmap = p.has_nmap; xj.do_matcap = p.do_matcap;
#if PICO3D_MULTICORE
    if (g_cores == 2 && mesh->vertex_count >= 64) {
      uint32_t half = mesh->vertex_count >> 1;
      g_xj = &xj; g_xv0 = half; g_xv1 = mesh->vertex_count;
      __sync_synchronize();
      ::pv_core1_run(pico3d_core1_xform);
      transform_range(xj, 0, half);
      { PICO3D_PD_START(pd_w); ::pv_core1_join(); PICO3D_PD_ADD(PICO3D_PD_WAIT, pd_w); }
    } else
#endif
      transform_range(xj, 0, mesh->vertex_count);
    if (flat_depth) {
      // The whole submission takes its nearest vertex's depth: every triangle
      // then lies on one depth plane, so meshes layer like cutouts instead of
      // interpenetrating (a motion-trail's silhouettes, a backdrop). Entries
      // behind the near plane keep their sentinel and are clipped as usual.
      float zmin = 3.4e38f;
      for (uint32_t i = 0; i < mesh->vertex_count; i++) {
        const pico3d_vcache_t &e = vcache_at(vb, vs, i);
        if (!parked(e.sxq) && e.z < zmin) zmin = e.z;
      }
      if (zmin < 3.4e38f) {
        for (uint32_t i = 0; i < mesh->vertex_count; i++) {
          pico3d_vcache_t &e = const_cast<pico3d_vcache_t &>(vcache_at(vb, vs, i));
          if (!parked(e.sxq)) e.z = zmin;
        }
      }
    }
#if PICO3D_PROF
    pico3d_prof_transform_cyc[PICO3D_PC] += pico3d_prof_cyc() - c0;
#endif

    // --- each triangle's screen-row extent, split across both cores --------
    // Each triangle reads its own three vertices and writes only its own two
    // extent entries, so the halves never touch the same memory; the only
    // shared result is the rows touched, which each core keeps for itself and
    // this one merges after the join. It has to follow the transform's join,
    // since a triangle's vertices can come from either core's half.
    extents_job_t ej;
    ej.vb = vb;                 ej.vs = vs;
    ej.ind = indices;           ej.ys = sc->ys + (size_t)sc->tri_count * 2;
    ej.cull_back = !material->double_sided;
    ej.tw = (float)t->width;
    const uint32_t T = mesh->triangle_count;
    int32_t ymin, ymax, xmin, xmax;
#if PICO3D_MULTICORE
    if (g_cores == 2 && T >= 64) {
      g_ej = &ej; g_ef0 = T >> 1; g_ef1 = T;
      __sync_synchronize();
      ::pv_core1_run(pico3d_core1_extents);
      extents_range(ej, 0, T >> 1, ymin, ymax, xmin, xmax);
      { PICO3D_PD_START(pd_w); ::pv_core1_join(); PICO3D_PD_ADD(PICO3D_PD_WAIT, pd_w); }
      if (g_eymin < ymin) ymin = g_eymin;
      if (g_eymax > ymax) ymax = g_eymax;
      if (g_exmin < xmin) xmin = g_exmin;
      if (g_exmax > xmax) xmax = g_exmax;
    } else
#endif
      extents_range(ej, 0, T, ymin, ymax, xmin, xmax);
    if (ymin < sc->ymin) sc->ymin = ymin;
    if (ymax > sc->ymax) sc->ymax = ymax;
    if (xmin < sc->xmin) sc->xmin = xmin;
    if (xmax > sc->xmax) sc->xmax = xmax;
    pico3d_sub_t *sub = &sc->subs[sc->sub_count++];
    sub->mesh = mesh;             sub->material = material;   sub->light = light;
    sub->rlight = p.rlight;       sub->raster_lit = p.raster_lit;
    sub->L = p.L;                 sub->shading = p.shading;
    sub->do_nmap = p.do_nmap;     sub->do_matcap = p.do_matcap;
    sub->vbase = sc->vert_bytes;  sub->vstride = vs;
    sub->indices = indices;
    sub->mvp = p.mvp;             sub->tbase = sc->tri_count;
    sc->vert_bytes += mesh->vertex_count * vs;
    sc->vert_count += mesh->vertex_count;
    sc->tri_count += mesh->triangle_count;
    PICO3D_PD_ADD(PICO3D_PD_ADD_WALL, pd_add);
    return true;
  }

  // Which triangles touch rows [by, be), per submission, concatenated into
  // `bin` as each submission's [which] slice. Two compares each, off a tight
  // sequential array - far cheaper than the per-triangle setup it saves.
  static void __not_in_flash_func(bin_rows)(pico3d_scene_t *sc, int by, int be,
                                            int which, uint16_t *bin) {
    uint32_t used = 0;
    for (uint32_t si = 0; si < sc->sub_count; si++) {
      pico3d_sub_t &sub = sc->subs[si];
      const int16_t *ys = sc->ys + (size_t)sub.tbase * 2;
      sub.boff[which] = used;
      for (uint32_t f = 0, T = sub.mesh->triangle_count; f < T; f++) {
        if (ys[f*2] < be && ys[f*2+1] >= by) bin[used++] = (uint16_t)f;
      }
      sub.bcount[which] = used - sub.boff[which];
    }
  }

  // Fill the target's rows from the [which] slices of `bin`, for one core.
  static int __not_in_flash_func(draw_band)(pico3d_scene_t *sc, const pico3d_target_t *bt,
                                            int which, const uint16_t *bin) {
    int drawn = 0;
    for (uint32_t si = 0; si < sc->sub_count; si++) {
      const pico3d_sub_t &sub = sc->subs[si];
      if (!sub.bcount[which]) continue;
      pass2_job_t job;
      job.target = *bt;             job.mesh = sub.mesh;
      job.vc = (const char *)sc->verts + sub.vbase;
      job.ind = sub.indices;
      job.vs = sub.vstride;         job.mvp = sub.mvp;
      job.material = sub.material;  job.light = sub.light;
      job.raster_light = sub.raster_lit ? &sub.rlight : nullptr;
      job.L = sub.L;                job.shading = sub.shading;
      job.tw = sc->tw;              job.th = sc->th;
      job.fog = bt->fog;            job.fog_far = bt->fog_far;
      job.fog_scale = (bt->fog_far > bt->fog_near) ? 1.0f / (bt->fog_far - bt->fog_near) : 0.0f;
      job.do_nmap = sub.do_nmap;    job.do_matcap = sub.do_matcap;
      drawn += draw_pass2(job, bin + sub.boff[which], sub.bcount[which]);
    }
    return drawn;
  }

  // One core's whole share of a band's rows: clear their depth, bin the
  // triangles that touch them, fill.
  static int __not_in_flash_func(draw_rows)(pico3d_scene_t *sc, const pico3d_target_t *t,
                                            uint16_t clear_to, int which, uint16_t *bin) {
    PICO3D_PD_START(pd_c);
    pico3d_depth_clear(const_cast<pico3d_target_t *>(t), clear_to);
    PICO3D_PD_ADD(PICO3D_PD_CLEAR, pd_c);
    PICO3D_PD_START(pd_b);
    bin_rows(sc, t->clip_y0, t->clip_y1, which, bin);
    PICO3D_PD_ADD(PICO3D_PD_BIN, pd_b);
    return draw_band(sc, t, which, bin);
  }

#if PICO3D_MULTICORE
  namespace {
    // own: core1 has its own half of the band and bin1, so it clears and bins
    // for itself; otherwise it fills alternate rows off core0's shared bin.
    struct band_job_t { pico3d_scene_t *sc; pico3d_target_t t; uint16_t clear_to; bool own; };
    band_job_t g_band;
    volatile int g_band_drawn1;
    void __not_in_flash_func(pico3d_core1_band)() {
      g_band_drawn1 = g_band.own
          ? draw_rows(g_band.sc, &g_band.t, g_band.clear_to, 1, g_band.sc->bin1)
          : draw_band(g_band.sc, &g_band.t, 0, g_band.sc->bin);
    }
  }
  // Below this many rows a band is not worth two cores: the second core's share
  // of the fill stops covering the per-triangle setup it has to repeat.
  static const int PICO3D_BAND_MIN_SPLIT = 8;

  // Two cores sharing a queue of bands. The depth strip is cut in two, one per
  // core; the scene's rows are cut into bands no taller than that; and each
  // core takes the next undrawn band off a shared counter, clears, bins and
  // fills it alone, and comes back for another until none are left. So a core
  // is only ever idle at the very end, for at most one band, and there is one
  // join a draw rather than one a band. A band is always drawn whole by one
  // core, so the pixels do not depend on which core got it.
  namespace {
    struct queue_job_t {
      pico3d_scene_t *sc; pico3d_target_t t;   // t.depth: the whole strip, both halves
      int y0, y1, band_h, strip_rows;          // rows to draw, a band's height, a core's strip
      uint16_t clear_to;
    };
    queue_job_t g_queue;
    int g_next_band;                           // shared: the next band to take
    volatile int g_queue_drawn1;

    int __not_in_flash_func(draw_queue)(int which) {
      const queue_job_t &q = g_queue;
      uint16_t *bin = which ? q.sc->bin1 : q.sc->bin;
      int drawn = 0;
      for (;;) {
        int k = __atomic_fetch_add(&g_next_band, 1, __ATOMIC_RELAXED);
        int by = q.y0 + k * q.band_h;
        if (by >= q.y1) break;
        int be = by + q.band_h;
        if (be > q.y1) be = q.y1;
        pico3d_target_t bt = q.t;
        bt.clip_y0 = by;
        bt.clip_y1 = be;
        bt.depth = q.t.depth + (size_t)which * (size_t)q.strip_rows * (size_t)q.t.depth_stride;
        bt.depth_y0 = by;                      // this core's strip starts at the band's first row
        drawn += draw_rows(q.sc, &bt, q.clear_to, which, bin);
      }
      return drawn;
    }
    void __not_in_flash_func(pico3d_core1_queue)() { g_queue_drawn1 = draw_queue(1); }
  }
#endif

  int pico3d_scene_draw(pico3d_scene_t *sc, pico3d_target_t *t, int band_rows,
                        uint16_t clear_to) {
    int y0 = t->clip_y0 < 0 ? 0 : t->clip_y0;
    int y1 = t->clip_y1 > t->height ? t->height : t->clip_y1;
    if (y1 <= y0) return 0;
    // The scene knows which columns anything touches: narrow the clip to
    // them (on top of whatever the caller clipped), so the raster and every
    // band's depth clear cover only the used part of each row.
    if (sc->xmin > t->clip_x0) t->clip_x0 = sc->xmin;
    if (sc->xmax + 1 < t->clip_x1) t->clip_x1 = sc->xmax + 1;
    if (t->clip_x1 <= t->clip_x0) return 0;
    const int rows = y1 - y0;
    // Only take charge of depth_y0 when actually banding. One band that covers
    // the clip leaves it as the caller set it, so a full-surface depth buffer
    // still works and a caller cannot accidentally have its rows moved.
    const bool banded = band_rows > 0 && band_rows < rows;
    if (!banded) band_rows = rows;
    int drawn = 0;
    PICO3D_PD_START(pd_dw);
    if (banded) {
      // Only the rows something touches (a triangle's extent is [lo, hi], so
      // hi + 1 bounds it), in as few bands as the strip allows, each about the
      // same height. Nothing persists between banded draws - each band clears
      // its own strip - so rows left out are simply not drawn.
      if (sc->ymin > y0) y0 = sc->ymin;
      if (sc->ymax + 1 < y1) y1 = sc->ymax + 1;
      if (y1 <= y0) return 0;
      const int used = y1 - y0;

#if PICO3D_MULTICORE
      // Two cores: a queue of bands, each core with its own half of the strip.
      const int strip = band_rows / 2;
      if (g_cores == 2 && sc->bin1 && t->row_step <= 1 && strip >= PICO3D_BAND_MIN_SPLIT) {
        int n = (used + strip - 1) / strip;
        if (n < g_work_bands) n = g_work_bands;
        int h = (used + n - 1) / n;
        if (h < PICO3D_BAND_MIN_SPLIT) h = PICO3D_BAND_MIN_SPLIT;
        if (h > strip) h = strip;
        g_queue.sc = sc;      g_queue.t = *t;
        g_queue.y0 = y0;      g_queue.y1 = y1;
        g_queue.band_h = h;   g_queue.strip_rows = strip;
        g_queue.clear_to = clear_to;
        g_next_band = 0;
        __sync_synchronize();                       // publish the job to core1
        ::pv_core1_run(pico3d_core1_queue);
        drawn += draw_queue(0);
        { PICO3D_PD_START(pd_w); ::pv_core1_join(); PICO3D_PD_ADD(PICO3D_PD_WAIT, pd_w); }
        drawn += g_queue_drawn1;
        PICO3D_PD_ADD(PICO3D_PD_DRAW_WALL, pd_dw);
        return drawn;
      }
#endif
      const int nbands = (used + band_rows - 1) / band_rows;
      band_rows = (used + nbands - 1) / nbands;
    }

    for (int by = y0; by < y1; by += band_rows) {
      int be = by + band_rows;
      if (be > y1) be = y1;
      pico3d_target_t bt = *t;          // inherits the caller's row_step/row_phase
      bt.clip_y0 = by;
      bt.clip_y1 = be;
      if (banded) bt.depth_y0 = by;

#if PICO3D_MULTICORE
      // Only take the row split over if the caller is not already using it, so
      // driving phases from outside (as the host tests do) still composes.
      if (g_cores == 2 && bt.row_step <= 1) {
        if (sc->bin1 && (be - by) >= 2 * PICO3D_BAND_MIN_SPLIT) {
          // One half of the band per core. Each clears, bins, sets up and fills
          // only the triangles touching its own rows, so only those straddling
          // the split are set up twice. The halves are disjoint rows of both the
          // colour target and the depth strip, so nothing needs locking.
          int mid = by + (be - by) / 2;
          pico3d_target_t t0 = bt, t1 = bt;
          t0.clip_y1 = mid;
          t1.clip_y0 = mid;
          g_band.sc = sc; g_band.t = t1; g_band.clear_to = clear_to; g_band.own = true;
          __sync_synchronize();                     // publish the job to core1
          ::pv_core1_run(pico3d_core1_band);
          drawn += draw_rows(sc, &t0, clear_to, 0, sc->bin);
          { PICO3D_PD_START(pd_w); ::pv_core1_join(); PICO3D_PD_ADD(PICO3D_PD_WAIT, pd_w); }
          drawn += g_band_drawn1;
          continue;
        }
        if ((be - by) >= PICO3D_BAND_MIN_SPLIT) {
          // No second bin: split the band's ROWS instead, each core filling every
          // other row off the one shared bin. Balanced whatever shape the scene
          // is, but both cores set up every triangle.
          pico3d_depth_clear(&bt, clear_to);
          bin_rows(sc, by, be, 0, sc->bin);
          pico3d_target_t t0 = bt, t1 = bt;
          t0.row_step = 2; t0.row_phase = by;       // this core: the band's first row on
          t1.row_step = 2; t1.row_phase = by + 1;   // core1: the other half
          g_band.sc = sc; g_band.t = t1; g_band.own = false;
          __sync_synchronize();
          ::pv_core1_run(pico3d_core1_band);
          drawn += draw_band(sc, &t0, 0, sc->bin);
          ::pv_core1_join();
          drawn += g_band_drawn1;
          continue;
        }
      }
#endif
      drawn += draw_rows(sc, &bt, clear_to, 0, sc->bin);
    }
    PICO3D_PD_ADD(PICO3D_PD_DRAW_WALL, pd_dw);
    return drawn;
  }

  // ---- depth buffer --------------------------------------------------------
  // Lives here rather than in the rasterisers: it is a property of the target,
  // not of how triangles are filled, and all three pico3d_raster*.cpp backends
  // otherwise carried an identical copy.
  //
  // Written a WORD at a time, from SRAM, and without calling out to memset.
  // The depth buffer is large (a 320x240 one is 150 KB) so an embedder may well
  // have it somewhere slower than SRAM - on an RP2350 with PSRAM it lands there
  // by default - and that makes two things true. A 16-bit store to a
  // cached-but-external window costs the same as a 32-bit one, so pairing them
  // halves the clear. And streaming 150 KB through the same small cache the CPU
  // fetches code through evicts the loop as it runs, so the loop and everything
  // it calls want to be resident: hence the hand-rolled fill over memset.
  static void __not_in_flash_func(depth_fill)(uint16_t *p, size_t n, uint16_t value) {
    if (!n) return;
    if (((uintptr_t)p & 3) != 0) { *p++ = value; if (--n == 0) return; }
    const uint32_t pair = (uint32_t)value | ((uint32_t)value << 16);
    uint32_t *w = (uint32_t *)p;
    for (size_t i = 0, words = n >> 1; i < words; i++) w[i] = pair;
    if (n & 1) p[n - 1] = value;
  }

  // Bounded by the target's clip rect, like every other entry point here - so a
  // clipped 3D viewport does not pay for rows it never draws, and one band of a
  // banded render can clear just its own rows. A target with a degenerate clip
  // clears nothing, so the clip has to be filled in (surface_view does).
  void __not_in_flash_func(pico3d_depth_clear)(pico3d_target_t *t, uint16_t value) {
    if (!t->depth) return;
    int x0 = t->clip_x0 < 0 ? 0 : t->clip_x0;
    int y0 = t->clip_y0 < 0 ? 0 : t->clip_y0;
    int x1 = t->clip_x1 > t->width  ? t->width  : t->clip_x1;
    int y1 = t->clip_y1 > t->height ? t->height : t->clip_y1;
    if (x1 <= x0 || y1 <= y0) return;
    // Depth row for surface row y is (y - depth_y0): the buffer may cover only
    // the band being drawn.
    const int dy0 = y0 - t->depth_y0;
    if (x0 == 0 && x1 == t->depth_stride) {     // whole rows: one contiguous run
      depth_fill(t->depth + (size_t)dy0 * (size_t)t->depth_stride,
                 (size_t)(y1 - y0) * (size_t)t->depth_stride, value);
      return;
    }
    for (int y = dy0; y < dy0 + (y1 - y0); y++)
      depth_fill(t->depth + (size_t)y * (size_t)t->depth_stride + x0,
                 (size_t)(x1 - x0), value);
  }

}
