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

  // Sutherland-Hodgman against that one plane. One vertex behind it leaves a
  // quad (4), two leave a smaller triangle (3), all three leave nothing (0).
  // Interpolating in clip space - before the perspective divide - is what keeps
  // the new vertices on the original triangle's plane.
  static int clip_near(const clipvert_t in[3], clipvert_t out[4]) {
    int n = 0;
    for (int i = 0; i < 3; i++) {
      const clipvert_t &a = in[i];
      const clipvert_t &b = in[(i + 1) % 3];
      float da = near_distance(a.clip), db = near_distance(b.clip);
      bool a_in = da >= 0.0f, b_in = db >= 0.0f;
      if (a_in) out[n++] = a;
      if (a_in != b_in) {
        float d = da - db;
        out[n++] = lerp_clipvert(a, b, d != 0.0f ? da / d : 0.0f);
      }
    }
    return n;
  }

  // Viewport map for a clipped vertex. Matches transform_range's cached
  // projection exactly, so a clipped triangle lands on the same pixels its
  // unclipped neighbours do and shared edges stay watertight.
  static inline void project_into(pico3d_tri_t &tri, int k, const clipvert_t &v,
                                  float tw, float th) {
    float w = 1.0f / v.clip.w;
    tri.sx[k]  = (v.clip.x * w * 0.5f + 0.5f) * tw;
    tri.sy[k]  = (1.0f - (v.clip.y * w * 0.5f + 0.5f)) * th;
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
    const pico3d_vcache_t   *vc;
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

      // The varyings, resolved once per vertex whichever path takes them. FLAT
      // takes its face normal from the unclipped triangle, so the light value is
      // the same for every piece a clip leaves behind.
      vec3_t vuv[3], vn[3], vtan[3];
      uint32_t vrgb[3];
      vuv[0] = uv(i0); vuv[1] = uv(i1); vuv[2] = uv(i2);
      if (do_nmap) {
        for (int k = 0; k < 3; k++) {
          vrgb[k] = vc[idx[k]].rgb; vn[k] = vc[idx[k]].nrm_w; vtan[k] = vc[idx[k]].tan_w;
        }
      } else if (do_matcap) {
        for (int k = 0; k < 3; k++) { vrgb[k] = vc[idx[k]].rgb; vuv[k] = vc[idx[k]].nrm_w; }
      } else if (shading == PICO3D_FLAT) {
        vec3_t n = (vc[i1].world - vc[i0].world).cross(vc[i2].world - vc[i0].world).normalized();
        uint32_t lv = pico3d_light_value(j.light, n.dot(L));
        for (int k = 0; k < 3; k++) vrgb[k] = pico3d_modulate(vc[idx[k]].rgb, lv);
      } else {                                          // UNLIT / GOURAUD pre-baked
        for (int k = 0; k < 3; k++) vrgb[k] = vc[idx[k]].rgb;
      }

      // Depth fog, after the light so it cannot pick up the surface's facing.
      // clip.w is the eye-space depth the projection already worked out, so this
      // costs a subtract, a clamp and a mix per vertex.
      if (j.fog_scale != 0.0f) {
        for (int k = 0; k < 3; k++) {
          float f = (j.fog_far - vc[idx[k]].clip.w) * j.fog_scale;
          f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
          vrgb[k] = lerp_rgb(j.fog, vrgb[k], f);
        }
      }

      if (near_distance(vc[i0].clip) >= 0.0f && near_distance(vc[i1].clip) >= 0.0f &&
          near_distance(vc[i2].clip) >= 0.0f) {
        pico3d_tri_t tri{};
        for (int k = 0; k < 3; k++) {                   // copy the CACHED screen projection
          tri.sx[k] = vc[idx[k]].sx; tri.sy[k] = vc[idx[k]].sy;
          tri.z[k]  = vc[idx[k]].z;  tri.iw[k] = vc[idx[k]].iw;
          tri.uv_[k] = vuv[k]; tri.rgb[k] = vrgb[k];
        }
        if (do_nmap) for (int k = 0; k < 3; k++) { tri.n[k] = vn[k]; tri.tan[k] = vtan[k]; }
        if (pico3d_raster_triangle(t, &tri, j.material, j.raster_light) > 0) drawn++;
        continue;
      }

      // Straddles the eye plane: cut it and fan what is left. The cached
      // projection is no use here, so each new vertex is projected on the spot.
      clipvert_t in[3], out[4];
      for (int k = 0; k < 3; k++) {
        in[k].clip = vc[idx[k]].clip;
        in[k].uv = vuv[k]; in[k].rgb = vrgb[k];
        in[k].n = do_nmap ? vn[k] : vec3_t(0, 0, 0);
        in[k].tan = do_nmap ? vtan[k] : vec3_t(0, 0, 0);
      }
      int m = clip_near(in, out);
      for (int k = 1; k + 1 < m; k++) {
        pico3d_tri_t tri{};
        project_into(tri, 0, out[0], j.tw, j.th);
        project_into(tri, 1, out[k], j.tw, j.th);
        project_into(tri, 2, out[k + 1], j.tw, j.th);
        if (pico3d_raster_triangle(t, &tri, j.material, j.raster_light) > 0) drawn++;
      }
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
    const uint32_t bin_cap = (uint32_t)(working_buffer_size / sizeof(uint16_t)) / 2;
    if (g_cores == 2 && job.mesh->triangle_count >= 8 &&
        job.mesh->triangle_count <= bin_cap) {
      int y0 = job.target.clip_y0, y1 = job.target.clip_y1;
      // Split at the MESH's on-screen vertical midpoint (not the screen's), then BIN each
      // triangle into the top and/or bottom half by its cached screen-Y bbox. Each core
      // then only sets up + fills the triangles in ITS band — so the per-triangle setup
      // is split between the cores instead of duplicated (only band-straddling triangles
      // are set up twice). Bins live in picovector's working buffer.
      const pico3d_vcache_t *vc = job.vc;
      float mny = 1e30f, mxy = -1e30f;
      for (uint32_t v = 0, nv = job.mesh->vertex_count; v < nv; v++) {
        // Only to pick where to split the screen, so a vertex with no meaningful
        // projection is simply left out of the extent.
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
        uint16_t *bot_bin = top_bin + bin_cap;
        uint32_t nt = 0, nb = 0;
        const uint16_t *ind = job.mesh->indices;
        for (uint32_t f = 0, T = job.mesh->triangle_count; f < T; f++) {
          uint16_t a = ind[f*3], b = ind[f*3+1], c = ind[f*3+2];
          if (near_distance(vc[a].clip) < 0.0f || near_distance(vc[b].clip) < 0.0f ||
              near_distance(vc[c].clip) < 0.0f) {
            // Crosses the near plane, so its cached sy is meaningless and there
            // is no telling which band the clipped pieces land in. Bin it to
            // both and let each core clip it against its own rows.
            top_bin[nt++] = (uint16_t)f;
            bot_bin[nb++] = (uint16_t)f;
            continue;
          }
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
    sc->tri_count = 0;
  }

  bool pico3d_scene_add(pico3d_scene_t *sc, const pico3d_target_t *t,
                        const pico3d_mesh_t *mesh, const mat4_t *model,
                        const mat4_t *view_proj, const pico3d_material_t *material,
                        pico3d_shading_t shading, const pico3d_light_t *light,
                        const mat4_t *view) {
    // Cull FIRST, before even the capacity checks: a mesh outside the frustum
    // takes no room in the scene, so a full scene should still swallow one
    // rather than report failure. This is the cheapest work in the pipeline and
    // it removes the most - ~60 operations against 560 cycles a vertex to
    // transform geometry that was never going to be seen. Reported as success:
    // nothing failed, there was simply nothing to add.
    prepared_t p = prepare_draw(mesh, model, view_proj, material, shading, light, view);
    if (pico3d_cull_mesh(mesh, &p.mvp)) return true;

    // Nothing here allocates: a full scene is refused so a frame can never
    // stall on a heap. bin holds one submission's live triangles, so it has to
    // fit the largest mesh rather than the whole scene.
    if (sc->sub_count >= sc->sub_cap) return false;
    if (sc->vert_count + mesh->vertex_count > sc->vert_cap) return false;
    if (sc->tri_count + mesh->triangle_count > sc->tri_cap) return false;
    if (mesh->triangle_count > sc->bin_cap) return false;

    pico3d_vcache_t *vc = sc->verts + sc->vert_count;
    sc->tw = (float)t->width;
    sc->th = (float)t->height;

    // --- pass 1, exactly as the immediate path runs it ------------------------
#if PICO3D_PROF
    uint32_t c0 = pico3d_prof_cyc();
#endif
    xform_job_t xj;
    xj.mesh = mesh;         xj.vc = vc;           xj.material = material;
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
      ::pv_core1_join();
    } else
#endif
      transform_range(xj, 0, mesh->vertex_count);
#if PICO3D_PROF
    pico3d_prof_transform_cyc += pico3d_prof_cyc() - c0;
#endif

    // --- each triangle's screen-row extent ------------------------------------
    // This is what lets a band skip a triangle for the price of two compares
    // instead of a full per-triangle setup, and it is why the extents live in
    // their own tight array rather than inside the vertex cache: a band scans
    // them start to end and touches nothing else.
    int16_t *ys = sc->ys + (size_t)sc->tri_count * 2;
    const uint16_t *ind = mesh->indices;
    for (uint32_t f = 0, T = mesh->triangle_count; f < T; f++) {
      uint16_t a = ind[f*3], b = ind[f*3+1], c = ind[f*3+2];
      if (near_distance(vc[a].clip) < 0.0f || near_distance(vc[b].clip) < 0.0f ||
          near_distance(vc[c].clip) < 0.0f) {
        // Crosses the near plane, so its cached sy means nothing and there is no
        // telling which rows the clipped pieces land in. Claim every band and
        // let each one clip it.
        ys[f*2] = -32768; ys[f*2+1] = 32767;
        continue;
      }
      float lo = vc[a].sy, hi = lo, sy;
      sy = vc[b].sy; if (sy < lo) lo = sy; else if (sy > hi) hi = sy;
      sy = vc[c].sy; if (sy < lo) lo = sy; else if (sy > hi) hi = sy;
      int ilo = (int)lo, ihi = (int)hi + 1;         // +1: round the far edge out
      ys[f*2]   = (int16_t)(ilo < -32768 ? -32768 : (ilo > 32767 ? 32767 : ilo));
      ys[f*2+1] = (int16_t)(ihi < -32768 ? -32768 : (ihi > 32767 ? 32767 : ihi));
    }

    pico3d_sub_t *sub = &sc->subs[sc->sub_count++];
    sub->mesh = mesh;             sub->material = material;   sub->light = light;
    sub->rlight = p.rlight;       sub->raster_lit = p.raster_lit;
    sub->L = p.L;                 sub->shading = p.shading;
    sub->do_nmap = p.do_nmap;     sub->do_matcap = p.do_matcap;
    sub->vbase = sc->vert_count;  sub->tbase = sc->tri_count;
    sc->vert_count += mesh->vertex_count;
    sc->tri_count += mesh->triangle_count;
    return true;
  }

  // One band's fill, for one core. Both cores run this over the SAME triangle
  // lists and the same band, differing only in target.row_phase, so they write
  // disjoint rows and need no coordination beyond the join.
  static int __not_in_flash_func(draw_band)(pico3d_scene_t *sc, const pico3d_target_t *bt) {
    int drawn = 0;
    for (uint32_t si = 0; si < sc->sub_count; si++) {
      const pico3d_sub_t &sub = sc->subs[si];
      if (!sub.bcount) continue;
      pass2_job_t job;
      job.target = *bt;             job.mesh = sub.mesh;
      job.vc = sc->verts + sub.vbase;
      job.material = sub.material;  job.light = sub.light;
      job.raster_light = sub.raster_lit ? &sub.rlight : nullptr;
      job.L = sub.L;                job.shading = sub.shading;
      job.tw = sc->tw;              job.th = sc->th;
      job.fog = bt->fog;            job.fog_far = bt->fog_far;
      job.fog_scale = (bt->fog_far > bt->fog_near) ? 1.0f / (bt->fog_far - bt->fog_near) : 0.0f;
      job.do_nmap = sub.do_nmap;    job.do_matcap = sub.do_matcap;
      drawn += draw_pass2(job, sc->bin + sub.boff, sub.bcount);
    }
    return drawn;
  }

#if PICO3D_MULTICORE
  namespace {
    struct band_job_t { pico3d_scene_t *sc; pico3d_target_t t; };
    band_job_t g_band;
    volatile int g_band_drawn1;
    void __not_in_flash_func(pico3d_core1_band)() {
      g_band_drawn1 = draw_band(g_band.sc, &g_band.t);
    }
  }
  // Below this many rows a band is not worth two cores: the second core's share
  // of the fill stops covering the per-triangle setup it has to repeat.
  static const int PICO3D_BAND_MIN_SPLIT = 8;
#endif

  int pico3d_scene_draw(pico3d_scene_t *sc, pico3d_target_t *t, int band_rows,
                        uint16_t clear_to) {
    int y0 = t->clip_y0 < 0 ? 0 : t->clip_y0;
    int y1 = t->clip_y1 > t->height ? t->height : t->clip_y1;
    if (y1 <= y0) return 0;
    const int rows = y1 - y0;
    // Only take charge of depth_y0 when actually banding. One band that covers
    // the clip leaves it as the caller set it, so a full-surface depth buffer
    // still works and a caller cannot accidentally have its rows moved.
    const bool banded = band_rows > 0 && band_rows < rows;
    if (!banded) band_rows = rows;

    int drawn = 0;
    for (int by = y0; by < y1; by += band_rows) {
      int be = by + band_rows;
      if (be > y1) be = y1;
      pico3d_target_t bt = *t;          // inherits the caller's row_step/row_phase
      bt.clip_y0 = by;
      bt.clip_y1 = be;
      if (banded) bt.depth_y0 = by;
      pico3d_depth_clear(&bt, clear_to);

      // Which triangles this band touches, per submission, concatenated into the
      // shared bin. Two compares each, off a tight sequential array - far
      // cheaper than the per-triangle setup it saves. Built once here so both
      // cores read the same lists.
      uint32_t used = 0;
      for (uint32_t si = 0; si < sc->sub_count; si++) {
        pico3d_sub_t &sub = sc->subs[si];
        const int16_t *ys = sc->ys + (size_t)sub.tbase * 2;
        sub.boff = used;
        for (uint32_t f = 0, T = sub.mesh->triangle_count; f < T; f++) {
          if (ys[f*2] < be && ys[f*2+1] >= by) sc->bin[used++] = (uint16_t)f;
        }
        sub.bcount = used - sub.boff;
      }

#if PICO3D_MULTICORE
      // Only take the row split over if the caller is not already using it, so
      // driving phases from outside (as the host tests do) still composes.
      if (g_cores == 2 && bt.row_step <= 1 && (be - by) >= PICO3D_BAND_MIN_SPLIT) {
        // Split the band's ROWS, not its geometry: each core fills every other
        // row of the whole band. Perfectly balanced whatever shape the scene is,
        // and the two cores touch disjoint rows of both the colour target and
        // the depth strip, so nothing needs locking.
        pico3d_target_t t0 = bt, t1 = bt;
        t0.row_step = 2; t0.row_phase = by;         // this core: the band's first row on
        t1.row_step = 2; t1.row_phase = by + 1;     // core1: the other half
        g_band.sc = sc; g_band.t = t1;
        __sync_synchronize();                       // publish the job to core1
        ::pv_core1_run(pico3d_core1_band);
        drawn += draw_band(sc, &t0);
        ::pv_core1_join();
        drawn += g_band_drawn1;
      } else
#endif
        drawn += draw_band(sc, &bt);
    }
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
