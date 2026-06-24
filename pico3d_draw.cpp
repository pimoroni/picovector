// pico3d mesh draw stage: transform -> light -> near-cull -> rasterise.
//
// Lighting is resolved here, per vertex, and baked into the triangle's vertex
// colours so the rasteriser stays shading-mode agnostic (see pico3d_raster.cpp).
//
// Near-plane handling (current limitation): a triangle with ANY vertex at or
// behind the near plane (clip.w <= NEAR_EPS) is dropped whole. That avoids the
// 1/w blow-up cheaply but pops geometry at screen edges. Proper near-clip that
// splits a straddling triangle into 1-2 new triangles is a follow-up; this is
// the natural place to insert it (before the raster call).

#include "pico3d.hpp"

#include <algorithm>
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
extern char PicoVector_working_buffer[];                 // 70 KB scratch (free during a 3D
#define PV_WORK_U16  (((50 + 20) * 1024) / 2)            // pass); reused here for the bins
#endif
#ifndef __not_in_flash_func
#define __not_in_flash_func(f) f
#endif

// TEMP phase profiler (cycle-accurate; see pico3d.hpp). draw_mesh times the
// vertex TRANSFORM (pass 1) + counts covered pixels; the per-triangle SETUP and
// scanline FILL are timed inside pico3d_raster_triangle. Read/zeroed by prof().
namespace picovector {

  uint64_t pico3d_prof_transform_cyc = 0;
  uint64_t pico3d_prof_build_cyc = 0;
  uint64_t pico3d_prof_project_cyc = 0;
  uint64_t pico3d_prof_planes_cyc = 0;
  uint64_t pico3d_prof_edges_cyc = 0;
  uint64_t pico3d_prof_fill_cyc = 0;
  uint64_t pico3d_prof_bbox_px = 0;
  uint64_t pico3d_prof_px = 0;

  using std::min;
  using std::max;

  static constexpr float NEAR_EPS = 1e-4f;

  // --- pass 2: per-triangle assembly + rasterise ----------------------------
  // Factored out of pico3d_draw_mesh so it can run on EITHER core over a band of
  // the target (the band is just the target's clip_y0/clip_y1 — the rasteriser
  // already clamps each triangle's bbox to it). Reads the shared (read-only) vertex
  // cache; writes only its band of the colour/depth buffers, so two cores over
  // disjoint bands never race. Plain data in/out -> safe to call from core1.
  struct pass2_job_t {
    pico3d_target_t          target;        // a COPY; clip_y0/clip_y1 select the band
    const pico3d_mesh_t     *mesh;
    const pico3d_vcache_t   *vc;
    const pico3d_material_t *material;
    const pico3d_light_t    *light;         // FLAT face lighting
    const pico3d_light_t    *raster_light;  // per-pixel lit path (or null)
    vec3_t                   L;
    pico3d_shading_t         shading;
    bool                     do_nmap, do_matcap;
  };

  // `bin` (or null = all triangles): a list of triangle indices this core should fill —
  // pre-binned to its band so each core only SETS UP the triangles in its half, instead
  // of every core setting up every triangle. That's how the per-triangle setup parallelises.
  static int __not_in_flash_func(draw_pass2)(const pass2_job_t &j, const uint16_t *bin, uint32_t bincount) {
    pico3d_target_t *t = (pico3d_target_t *)&j.target;
    const pico3d_mesh_t *mesh = j.mesh;
    const pico3d_vcache_t *vc = j.vc;
    bool do_nmap = j.do_nmap, do_matcap = j.do_matcap;
    pico3d_shading_t shading = j.shading;
    vec3_t L = j.L;
    auto uv = [&](uint32_t i) {
      return mesh->uvs ? vec3_t(mesh->uvs[i*2], mesh->uvs[i*2+1], 0.0f) : vec3_t(0,0,0);
    };
    uint32_t n = bin ? bincount : mesh->triangle_count;
    int drawn = 0;
    for (uint32_t bi = 0; bi < n; bi++) {
      uint32_t f = bin ? bin[bi] : bi;
      uint16_t i0 = mesh->indices[f*3], i1 = mesh->indices[f*3+1], i2 = mesh->indices[f*3+2];
      const uint16_t idx[3] = {i0, i1, i2};
      if (vc[i0].clip.w <= NEAR_EPS || vc[i1].clip.w <= NEAR_EPS || vc[i2].clip.w <= NEAR_EPS)
        continue;                                       // near-plane cull (whole triangle)
      pico3d_tri_t tri{};
      for (int k = 0; k < 3; k++) {                     // copy the CACHED screen projection
        tri.sx[k] = vc[idx[k]].sx; tri.sy[k] = vc[idx[k]].sy;
        tri.z[k]  = vc[idx[k]].z;  tri.iw[k] = vc[idx[k]].iw;
      }
      tri.uv_[0] = uv(i0); tri.uv_[1] = uv(i1); tri.uv_[2] = uv(i2);
      if (do_nmap) {
        for (int k = 0; k < 3; k++) {
          tri.rgb[k] = vc[idx[k]].rgb; tri.n[k] = vc[idx[k]].nrm_w; tri.tan[k] = vc[idx[k]].tan_w;
        }
      } else if (do_matcap) {
        for (int k = 0; k < 3; k++) { tri.rgb[k] = vc[idx[k]].rgb; tri.uv_[k] = vc[idx[k]].nrm_w; }
      } else if (shading == PICO3D_FLAT) {
        vec3_t n = (vc[i1].world - vc[i0].world).cross(vc[i2].world - vc[i0].world).normalized();
        uint32_t lv = pico3d_light_value(j.light, n.dot(L));
        for (int k = 0; k < 3; k++) tri.rgb[k] = pico3d_modulate(vc[idx[k]].rgb, lv);
      } else {                                          // UNLIT / GOURAUD pre-baked
        for (int k = 0; k < 3; k++) tri.rgb[k] = vc[idx[k]].rgb;
      }
      if (pico3d_raster_triangle(t, &tri, j.material, j.raster_light) > 0) drawn++;
    }
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
    if (g_cores == 2 && job.mesh->triangle_count >= 8 &&
        job.mesh->triangle_count <= PV_WORK_U16 / 2) {
      int y0 = job.target.clip_y0, y1 = job.target.clip_y1;
      // Split at the MESH's on-screen vertical midpoint (not the screen's), then BIN each
      // triangle into the top and/or bottom half by its cached screen-Y bbox. Each core
      // then only sets up + fills the triangles in ITS band — so the per-triangle setup
      // is split between the cores instead of duplicated (only band-straddling triangles
      // are set up twice). Bins live in picovector's 70 KB working buffer.
      const pico3d_vcache_t *vc = job.vc;
      float mny = 1e30f, mxy = -1e30f;
      for (uint32_t v = 0, nv = job.mesh->vertex_count; v < nv; v++) {
        if (vc[v].clip.w <= NEAR_EPS) continue;
        float sy = vc[v].sy;
        if (sy < mny) mny = sy;
        if (sy > mxy) mxy = sy;
      }
      if (mxy > mny) {
        int mid = (int)((mny + mxy) * 0.5f);
        if (mid < y0) mid = y0; else if (mid > y1) mid = y1;
        float midf = (float)mid;
        uint16_t *top_bin = (uint16_t *)PicoVector_working_buffer;
        uint16_t *bot_bin = top_bin + (PV_WORK_U16 / 2);
        uint32_t nt = 0, nb = 0;
        const uint16_t *ind = job.mesh->indices;
        for (uint32_t f = 0, T = job.mesh->triangle_count; f < T; f++) {
          uint16_t a = ind[f*3], b = ind[f*3+1], c = ind[f*3+2];
          if (vc[a].clip.w <= NEAR_EPS || vc[b].clip.w <= NEAR_EPS || vc[c].clip.w <= NEAR_EPS) continue;
          float lo = vc[a].sy, hi = lo, s;
          s = vc[b].sy; if (s < lo) lo = s; else if (s > hi) hi = s;
          s = vc[c].sy; if (s < lo) lo = s; else if (s > hi) hi = s;
          if (lo <  midf) top_bin[nt++] = (uint16_t)f;
          if (hi >= midf) bot_bin[nb++] = (uint16_t)f;
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

  // --- pass 1: transform (+ light) a VERTEX RANGE — factored so it can split across
  // cores. Each vertex writes its own vcache slot, so two cores over disjoint ranges
  // never race. (Embarrassingly parallel; this is the win for transform-bound meshes.)
  struct xform_job_t {
    const pico3d_mesh_t     *mesh;   pico3d_vcache_t *vc;
    const pico3d_material_t *material; const pico3d_light_t *light;
    const mat4_t            *model;  mat4_t mvp, mc_nrm;  vec3_t L;
    float                    tw, th;  // target width/height (for the cached screen projection)
    pico3d_shading_t         shading; bool do_nmap, has_nmap, do_matcap;
  };

  static void __not_in_flash_func(transform_range)(const xform_job_t &j, uint32_t v0, uint32_t v1) {
    const pico3d_mesh_t *mesh = j.mesh; pico3d_vcache_t *vc = j.vc;
    const pico3d_material_t *material = j.material; const pico3d_light_t *light = j.light;
    const mat4_t *model = j.model; const mat4_t &mvp = j.mvp; const mat4_t &mc_nrm = j.mc_nrm;
    bool do_nmap = j.do_nmap, has_nmap = j.has_nmap, do_matcap = j.do_matcap;
    pico3d_shading_t shading = j.shading; vec3_t L = j.L;
    auto nrm = [&](uint32_t i){ return vec3_t(mesh->normals[i*3], mesh->normals[i*3+1], mesh->normals[i*3+2]); };
    auto tan = [&](uint32_t i){ return vec3_t(mesh->tangents[i*3], mesh->tangents[i*3+1], mesh->tangents[i*3+2]); };
    for (uint32_t v = v0; v < v1; v++) {
      vec3_t p(mesh->positions[v*3], mesh->positions[v*3+1], mesh->positions[v*3+2]);
      vec4_t c = mvp * p; vc[v].clip = c;
      float w = (c.w > NEAR_EPS) ? 1.0f / c.w : 0.0f;    // pre-project to screen (cached so
      vc[v].iw = w;                                      // the rasteriser does no divide)
      vc[v].sx = (c.x * w * 0.5f + 0.5f) * j.tw;
      vc[v].sy = (1.0f - (c.y * w * 0.5f + 0.5f)) * j.th;
      vc[v].z  = c.z * w;
      uint32_t b = mesh->colors ? mesh->colors[v] : material->color;
      if (do_nmap) {
        vc[v].rgb = b;
        vc[v].nrm_w = model->transform_direction(nrm(v)).normalized();
        vc[v].tan_w = has_nmap ? model->transform_direction(tan(v)).normalized() : vec3_t(0, 0, 0);
      } else if (do_matcap) {
        vec3_t n = mc_nrm.transform_direction(nrm(v)).normalized();
        vc[v].rgb = b;
        vc[v].nrm_w = vec3_t(n.x * 0.5f + 0.5f, 0.5f - n.y * 0.5f, 0.0f);
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
        vc[v].rgb = pico3d_modulate(b, lv);
      } else if (shading == PICO3D_FLAT) {
        vc[v].world = (*model * p).xyz();
        vc[v].rgb = b;
      } else {
        vc[v].rgb = b;
      }
    }
  }

#if PICO3D_MULTICORE
  static const xform_job_t * volatile g_xj;              // vertex-transform job for core1
  static volatile uint32_t g_xv0, g_xv1;                 // core1's vertex range
  static void __not_in_flash_func(pico3d_core1_xform)() {
    transform_range(*g_xj, g_xv0, g_xv1);
  }
#endif

  int pico3d_draw_mesh(pico3d_target_t *t, const pico3d_mesh_t *mesh,
                    const mat4_t *model, const mat4_t *view_proj,
                    const pico3d_material_t *material, pico3d_shading_t shading,
                    const pico3d_light_t *light, pico3d_vcache_t *vc,
                    const mat4_t *view) {
    mat4_t mvp = (*view_proj) * (*model);

    // Normal mapping needs a map, a light, and per-vertex normals+tangents. When
    // active it overrides the shading mode (lighting is done per-pixel in the
    // rasteriser); the rasteriser only treats this triangle as normal-mapped when
    // we pass it `light` (raster_light below), so the gate is exact.
    // Per-pixel lit path: a normal map (needs tangents) OR Blinn-Phong specular (needs
    // the per-pixel normal). Both light per pixel in the rasteriser.
    bool has_nmap = material->normal_map && mesh->tangents;
    bool has_spec = material->specular != 0;
    bool do_nmap = (has_nmap || has_spec) && light && mesh->normals;
    // Per-draw light copy so we can stash the specular half-vector (H = normalise(L+V),
    // V = the world direction toward the camera = the view matrix's row 2).
    pico3d_light_t rlight = light ? *light : pico3d_light_t{};
    if (has_spec && view && light) {
      vec3_t Lh = (-light->direction).normalized();
      rlight.half = (Lh + vec3_t(view->v20, view->v21, view->v22)).normalized();
    }
    const pico3d_light_t *raster_light = do_nmap ? &rlight : nullptr;

    // Matcap needs a map and per-vertex normals; normal_map wins if both are set.
    // Normals go to VIEW space (so the reflection tracks the camera) when a view
    // matrix is supplied, else they stay in world space.
    bool do_matcap = !do_nmap && material->matcap && mesh->normals;
    mat4_t mc_nrm = do_matcap ? (view ? (*view) * (*model) : *model) : mat4_t();

    // Gouraud needs per-vertex normals; without them, fall back to flat.
    if (shading == PICO3D_GOURAUD && !mesh->normals) shading = PICO3D_FLAT;
    if (!light) shading = PICO3D_UNLIT;

#if PICO3D_PROF
    uint32_t c0 = pico3d_prof_cyc();
#endif
    // --- pass 1: transform + light every vertex — split across both cores in 2-core --
    vec3_t L = light ? (-light->direction).normalized() : vec3_t(0, 0, 0);
    xform_job_t xj;
    xj.mesh = mesh;       xj.vc = vc;             xj.material = material; xj.light = light;
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
      ::pv_core1_join();
    } else
#endif
      transform_range(xj, 0, mesh->vertex_count);

#if PICO3D_PROF
    pico3d_prof_transform_cyc += pico3d_prof_cyc() - c0;
#endif
    // --- pass 2: assemble + rasterise (optionally split across both cores) -----
    pass2_job_t job;
    job.target = *t;          job.mesh = mesh;     job.vc = vc;
    job.material = material;   job.light = light;   job.raster_light = raster_light;
    job.L = L;                 job.shading = shading;
    job.do_nmap = do_nmap;     job.do_matcap = do_matcap;
    return pico3d_dispatch_pass2(job);
  }

}
