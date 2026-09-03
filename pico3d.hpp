#pragma once

// pico3d — a small fixed-function 3D rasteriser for Badgeware.
//
// The engine is deliberately decoupled from picovector's image_t: it renders
// into plain pointer "views" (pico3d_target_t / pico3d_texture_t) so the hot loop
// builds and unit-tests on the host with no Pico SDK present. The MicroPython
// binding layer is the only place that knows about image_t — it fills these
// views from image_t::ptr()/bounds() and a caller-owned depth buffer.
//
// Colour format matches the Tufty framebuffer word: 0x00BBGGRR
//   (R = bits 0..7, G = 8..15, B = 16..23). See st7789.cpp ST_RGB565.

#include <stdint.h>

#include "vec3.hpp"
#include "mat4.hpp"

namespace picovector {

  // --- colour helpers (0x00BBGGRR) -----------------------------------------
  static inline uint32_t pico3d_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
  }

  // exact x/255 for x in [0, 65025] (a product of two bytes), with no integer
  // divide: (x + (x>>8) + 1) >> 8. Host-verified equal to x/255 over that range.
  static inline uint32_t pico3d_div255(uint32_t x) { return (x + (x >> 8) + 1) >> 8; }

  // per-channel (a*b)/255 — used to tint a colour by a light value or texel.
  static inline uint32_t pico3d_modulate(uint32_t a, uint32_t b) {
    uint32_t r  = pico3d_div255((a       & 0xff) * (b       & 0xff)) & 0xff;
    uint32_t g  = pico3d_div255(((a >> 8) & 0xff) * ((b >> 8) & 0xff)) & 0xff;
    uint32_t bl = pico3d_div255(((a >> 16)& 0xff) * ((b >> 16)& 0xff)) & 0xff;
    return r | (g << 8) | (bl << 16);
  }

  // --- views ----------------------------------------------------------------

  // Colour + depth render target. `color` and `depth` are caller-owned, row
  // strides in elements (not bytes). depth == nullptr disables depth testing.
  struct pico3d_target_t {
    uint32_t *color;
    uint16_t *depth;       // 0x0000 = near, 0xFFFF = far; nullptr to disable
    int width, height;     // full surface size
    int color_stride;      // elements per row in `color`
    int depth_stride;      // elements per row in `depth`
    // clip rectangle (inclusive min, exclusive max), clamped to surface
    int clip_x0, clip_y0, clip_x1, clip_y1;
    // Which surface row `depth` row 0 is. Zero for a full-surface depth buffer;
    // set to a band's first row when the depth buffer covers only that band, so
    // a 320x240 render can depth-test against a 320x80 buffer three times over
    // and keep it in fast memory. The colour target is always full-surface.
    int depth_y0;
    // Fill only every row_step'th row, starting from the one congruent to
    // row_phase. 0 or 1 means every row. This is how two cores share ONE band:
    // give each the same triangles and opposite phases and they write disjoint
    // rows of both the colour target and the depth buffer, needing no lock and
    // no split of the geometry. Per-triangle setup is then done twice, once per
    // core, which is the price of perfect load balance - cheap, because setup is
    // ~666 cycles against 40-79 cycles a pixel of fill. Splitting a band's ROWS
    // beats splitting the screen into bands per core, whose work is very uneven.
    int row_step, row_phase;
    // Linear depth fog, mixed in per vertex after the light: a colour every
    // surface fades towards with distance, and the two distances the ramp runs
    // between. fog_far <= fog_near turns it off, which is the zeroed default.
    //
    // Fog belongs to the view, not to a light or a material - a point light at
    // the eye looks like distance fade until you notice its falloff is scaled by
    // n.L, so a wall you face square-on comes out brighter than one seen
    // edge-on at the same distance. This is applied after the light and depends
    // only on depth, so orientation cannot leak into it.
    uint32_t fog;
    float fog_near, fog_far;
  };

  // Texture as a plain RGBA8888 (0x00BBGGRR) pixel block, edge-clamped.
  struct pico3d_texture_t {
    const uint32_t *texels;
    int width, height;
  };

  typedef enum { PICO3D_NEAREST = 0, PICO3D_BILINEAR = 1 } pico3d_filter_t;

  // Material. Lighting is resolved per-vertex in the vertex stage and arrives
  // at the rasteriser baked into the per-vertex colours, so the rasteriser is
  // shading-mode agnostic — it always modulates the interpolated vertex colour
  // by the (optional) texture sample.
  struct pico3d_material_t {
    const pico3d_texture_t *texture;  // nullptr → flat/vertex colour only
    uint32_t color;                // tint / base colour (0x00BBGGRR)
    pico3d_filter_t filter;
    bool double_sided;             // disable back-face culling
    uint8_t alpha_cutoff;          // discard texels with alpha < this (0 = off).
                                   // Alpha-cutout for foliage/cards; no blending.
    // Optional tangent-space normal map. When set (AND the mesh has tangents and
    // a light is supplied), lighting becomes PER-PIXEL using the perturbed normal.
    // When null, none of the normal-map machinery runs — the fast vertex-lit path
    // is byte-for-byte unchanged.
    const pico3d_texture_t *normal_map;
    // Optional matcap / spherical environment map. When set (and the mesh has
    // normals), the surface is shaded PER-PIXEL by sampling this texture with the
    // interpolated normal: uv = (n.x*0.5+0.5, 0.5-n.y*0.5), where n is in view
    // space if a view matrix is passed to pico3d_draw_mesh, else world space. The
    // sample MODULATES the base colour, so a white base gives a pure reflection and
    // a coloured base a tinted one. Independent of `light`. Mutually exclusive with
    // normal_map (normal_map wins if both are set).
    const pico3d_texture_t *matcap;
    // Optional Blinn-Phong specular. When `specular` != 0 (AND a light is supplied
    // AND a `view` matrix is passed to pico3d_draw_mesh), a per-pixel highlight of
    // colour `specular` and tightness `shininess` is ADDED on top of the diffuse,
    // using the interpolated (or normal-mapped) per-pixel normal. Independent of the
    // texture; pairs with or without a normal map.
    uint32_t specular;   // specular colour (0x00BBGGRR); 0 = off
    int32_t  shininess;  // Blinn-Phong exponent (higher = tighter highlight)
  };

  // A single triangle ready to rasterise: clip-space positions plus the
  // per-vertex varyings (uv + already-lit colour). The world-space normal/tangent
  // are only filled (and interpolated) when a normal map is in use.
  struct pico3d_tri_t {
    // Pre-projected screen-space vertices (computed once per vertex in the transform
    // pass and cached, so the rasteriser does NO perspective divide — the big setup win
    // for high-vertex meshes, and it parallelises because the transform pass is split).
    float    sx[3], sy[3];   // screen pixel position
    float    z[3], iw[3];    // depth (z/w) and 1/w (for perspective-correct interpolation)
    vec3_t   uv_[3];    // .x=u .y=v (.z unused, keeps it cheap to pass)
    uint32_t rgb[3];    // per-vertex colour, lighting pre-applied (unless normal-mapped)
    vec3_t   n[3];      // per-vertex world normal   (normal mapping only)
    vec3_t   tan[3];    // per-vertex world tangent  (normal mapping only)
  };

  // --- mesh / lighting ------------------------------------------------------

  // Borrowed, indexed geometry. All pointers are caller-owned (typically a
  // MicroPython array/memoryview filled by the Python .obj loader); pico3d never
  // takes ownership or copies. normals/uvs may be null.
  struct pico3d_mesh_t {
    const float    *positions;  // 3 * vertex_count  (x,y,z)
    const float    *normals;    // 3 * vertex_count  (nx,ny,nz) or null
    const float    *uvs;        // 2 * vertex_count  (u,v)      or null
    const uint32_t *colors;     // vertex_count, 0x00BBGGRR, or null (-> material.color)
    const float    *tangents;   // 3 * vertex_count  (tx,ty,tz) or null (for normal maps)
    const uint16_t *indices;    // 3 * triangle_count
    uint32_t        vertex_count;
    uint32_t        triangle_count;
    // Model-space bounding box, for whole-mesh frustum culling. `has_bounds`
    // gates it so a zero-initialised mesh simply is not culled - the safe
    // default, since an empty box at the origin would cull everything.
    // Positions stay writable (a mesh animates in place), so anything that
    // rewrites them past the box has to recompute: see pico3d_mesh_bounds.
    float           bmin[3], bmax[3];
    uint8_t         has_bounds;
  };

  typedef enum {
    PICO3D_FLAT    = 0,  // one geometric face normal -> one light value / triangle
    PICO3D_GOURAUD = 1,  // per-vertex normal -> light interpolated across the face
    PICO3D_UNLIT   = 2   // no lighting; material colour (× texture) straight through
  } pico3d_shading_t;

  // Directional OR point light + ambient term. Colours are 0x00BBGGRR.
  struct pico3d_light_t {
    vec3_t   direction;   // direction the light travels (world space; directional)
    uint32_t color;       // diffuse colour
    uint32_t ambient;     // ambient colour added unconditionally
    vec3_t   position;    // point-light world position (used when `point` != 0)
    float    atten;       // point-light falloff k: factor = 1/(1 + k*dist^2)
    uint8_t  point;       // 0 = directional, 1 = point light
    vec3_t   half;        // INTERNAL: Blinn-Phong half-vector, filled by the draw stage
  };

  // ambient + diffuse*max(0,ndotl)*atten, per channel, clamped. Shared by the vertex
  // stage and the per-pixel lit path. `atten` is the point-light falloff (1 = none).
  static inline uint32_t pico3d_light_value(const pico3d_light_t *l, float ndotl,
                                            float atten = 1.0f) {
    if (ndotl < 0.0f) ndotl = 0.0f;
    ndotl *= atten;
    auto ch = [&](uint32_t amb, uint32_t dif) -> uint32_t {
      int v = (int)((amb & 0xff) + (dif & 0xff) * ndotl);
      return (uint32_t)(v > 255 ? 255 : v);
    };
    return ch(l->ambient,       l->color)
         | (ch(l->ambient >> 8,  l->color >> 8)  << 8)
         | (ch(l->ambient >> 16, l->color >> 16) << 16);
  }

  // Add a Blinn-Phong specular highlight (colour `spec`) to an existing 0x00BBGGRR
  // colour: spec * (max(0, n·h))^shininess. Integer pow by repeated squaring (no libm).
  static inline uint32_t pico3d_add_specular(uint32_t col, uint32_t spec,
                                             float ndoth, int shininess) {
    if (ndoth <= 0.0f) return col;
    float p = ndoth, acc = 1.0f;
    for (int e = shininess; e; e >>= 1) { if (e & 1) acc *= p; p *= p; }
    int s = (int)(acc * 256.0f);                          // .8 specular strength
    auto ch = [&](int base, uint32_t sc) -> uint32_t {
      int v = base + (((int)(sc & 0xff) * s) >> 8);
      return (uint32_t)(v > 255 ? 255 : v);
    };
    return ch(col & 0xff,         spec)
         | (ch((col >> 8) & 0xff,  spec >> 8)  << 8)
         | (ch((col >> 16) & 0xff, spec >> 16) << 16);
  }

  // Per-vertex transform-cache scratch: the caller passes an array of at least
  // mesh->vertex_count of these to pico3d_draw_mesh so each unique vertex is
  // transformed (and lit, for non-flat) ONCE per frame rather than once per
  // triangle that references it — a big win for complex shared-vertex meshes.
  struct pico3d_vcache_t {
    vec4_t   clip;    // clip-space position
    float    sx, sy;  // pre-projected screen position (computed once here, not per triangle)
    float    z, iw;   // depth (z/w) and 1/w
    vec3_t   world;   // world-space position (only used by FLAT for face normals)
    uint32_t rgb;     // per-vertex colour: final for UNLIT/GOURAUD, base for FLAT/normal-mapped
    vec3_t   nrm_w;   // world normal  (only filled when normal-mapping)
    vec3_t   tan_w;   // world tangent (only filled when normal-mapping)
  };

  // --- API ------------------------------------------------------------------

  // Clear the depth buffer (no-op if target has none).
  void pico3d_depth_clear(pico3d_target_t *t, uint16_t value = 0xFFFF);

  // TEMP phase profiler — CYCLE-accurate (DWT CYCCNT on the M33; host no-op).
  // Accumulators read+zeroed by the pico3d.prof() binding:
  //   transform = pass-1 vertex transform (MVP, lighting) -> vcache
  //   build     = pass-2 per-triangle assembly (read vcache, near-cull)
  //   setup, split 3 ways:
  //     project = 3x 1/w + screen map + backface cull + bbox
  //     planes  = attribute gradient setup (z/uv/rgb/normal-tangent)
  //     edges   = edge functions + shade context
  //   fill      = the scanline rasterise (coverage + per-pixel emit)
  //   bbox_px   = total bbox pixels iterated by fill (vs px = those written)
  //   px        = covered pixels actually written
#if defined(__arm__)
  static inline uint32_t pico3d_prof_cyc() {
    static bool en = false;
    if (!en) {                                            // enable DWT cycle counter once
      *(volatile uint32_t *)0xE000EDFCu |= (1u << 24);    //   DEMCR.TRCENA
      *(volatile uint32_t *)0xE0001000u |= 1u;            //   DWT_CTRL.CYCCNTENA
      en = true;
    }
    return *(volatile uint32_t *)0xE0001004u;             //   DWT_CYCCNT
  }
  #define PICO3D_PROF 1
#else
  static inline uint32_t pico3d_prof_cyc() { return 0; }
  #define PICO3D_PROF 0
#endif
  extern uint64_t pico3d_prof_transform_cyc, pico3d_prof_build_cyc,
                  pico3d_prof_project_cyc, pico3d_prof_planes_cyc, pico3d_prof_edges_cyc,
                  pico3d_prof_fill_cyc, pico3d_prof_bbox_px, pico3d_prof_px;

  // Transform, light, near-cull and rasterise an indexed mesh in one call.
  // `model` places the mesh in the world; `view_proj` is camera × projection.
  // `light` may be null (treated as unlit). Triangles with any vertex at/behind
  // the near plane are dropped for now (see note in pico3d_draw.cpp). Returns the
  // number of triangles actually rasterised.
  // `vcache` must point to at least mesh->vertex_count pico3d_vcache_t entries
  // (caller-owned scratch); it is overwritten each call.
  // `view` (optional) is the camera/view matrix on its own (NOT multiplied into
  // view_proj). It is only used for matcap materials, to take normals into view
  // space so the reflection tracks the camera; pass null for world-space matcap.
  int pico3d_draw_mesh(pico3d_target_t *t, const pico3d_mesh_t *mesh,
                    const mat4_t *model, const mat4_t *view_proj,
                    const pico3d_material_t *material, pico3d_shading_t shading,
                    const pico3d_light_t *light, pico3d_vcache_t *vcache,
                    const mat4_t *view = nullptr);

  // --- deferred scene (banded rendering) ------------------------------------
  //
  // Why this exists: the depth buffer is the most expensive memory a render
  // touches - one read and one write for every pixel covered - so it wants to
  // be in the fastest memory available. On a board where that memory is far too
  // small to hold a full-screen depth buffer, the way out is to depth-test
  // against a BAND of rows at a time and run the geometry past each band in
  // turn. That only works if all the geometry is known before any band is
  // rasterised, which is what a scene is for.
  //
  // Flow: reset(), add() each mesh (transforms and projects it once, into the
  // scene's own vertex arena), then draw(), which walks the bands. The arena is
  // read once per band, sequentially, so it is fine for it to live in slower
  // memory - unlike a depth buffer, it is touched per vertex, not per pixel.

  // One submitted mesh: everything the raster pass needs, resolved at add() time
  // so a band never redoes it.
  struct pico3d_sub_t {
    const pico3d_mesh_t     *mesh;
    const pico3d_material_t *material;
    const pico3d_light_t    *light;
    pico3d_light_t           rlight;       // per-draw copy (holds the specular half-vector)
    bool                     raster_lit;   // rlight is live: lighting is per pixel
    vec3_t                   L;
    pico3d_shading_t         shading;
    bool                     do_nmap, do_matcap;
    uint32_t                 vbase;        // its vertices, from here in the arena
    uint32_t                 tbase;        // its triangles' screen-Y extents, from here
    // Scratch, refilled for each band: this submission's live triangles, as a
    // slice of the scene's shared bin. Built once by the dispatching core so
    // both cores can read the same list rather than each scanning for itself.
    uint32_t                 boff, bcount;
  };

  // Caller-owned storage. Every array is sized by the caller and never grown:
  // add() returns false rather than allocating, so a frame cannot stall on a
  // heap. `ys` holds two int16 a triangle (min and max screen row).
  struct pico3d_scene_t {
    pico3d_sub_t    *subs;   uint32_t sub_cap,  sub_count;
    pico3d_vcache_t *verts;  uint32_t vert_cap, vert_count;
    int16_t         *ys;     uint32_t tri_cap,  tri_count;
    uint16_t        *bin;    uint32_t bin_cap;   // scratch: one submission's live triangles
    float            tw, th;                     // viewport the vertices were projected for
  };

  // --- whole-mesh frustum culling -------------------------------------------

  // Fill in mesh->bmin/bmax from its positions and set has_bounds. One pass over
  // the vertices; call it once when the geometry is built, and again after
  // anything moves a vertex outside the old box.
  void pico3d_mesh_bounds(pico3d_mesh_t *mesh);

  // Is the mesh entirely outside the frustum of `mvp` (projection x view x
  // model)? False whenever it might be visible, or has no bounds - it never
  // culls something that should draw.
  //
  // The six planes are extracted from the MVP, which puts them in the mesh's
  // OWN space, so the box can be tested where it was measured. Going via world
  // space would mean re-boxing a rotated box axis-aligned first, which is both
  // looser and more work. A whole mesh rejected here costs ~60 operations
  // instead of transforming every vertex at ~560 cycles each to discover it was
  // off-screen.
  bool pico3d_cull_mesh(const pico3d_mesh_t *mesh, const mat4_t *mvp);

  void pico3d_scene_reset(pico3d_scene_t *sc);

  // Transform, light and project one mesh into the scene. Returns false if any
  // of the scene's arrays is too small, having added nothing.
  bool pico3d_scene_add(pico3d_scene_t *sc, const pico3d_target_t *t,
                        const pico3d_mesh_t *mesh, const mat4_t *model,
                        const mat4_t *view_proj, const pico3d_material_t *material,
                        pico3d_shading_t shading, const pico3d_light_t *light,
                        const mat4_t *view = nullptr);

  // Rasterise the scene into `t`, `band_rows` rows at a time (<= 0 means one
  // band covering the whole clip). When `t->depth` is only band_rows tall, pass
  // its height as band_rows and this will point depth_y0 at each band in turn
  // and clear it - so a small, fast depth buffer serves a whole screen.
  // Returns the number of triangles rasterised.
  int pico3d_scene_draw(pico3d_scene_t *sc, pico3d_target_t *t, int band_rows,
                        uint16_t clear_to = 0xFFFF);

  // Rasterise the per-triangle pass on both cores (n=2, top/bottom screen bands) or
  // the calling core only (n=1, default). Geometry/setup is duplicated per core, so
  // the win is on FILL-bound scenes. No-op (always 1 core) on host.
  void pico3d_set_cores(int n);
  int  pico3d_get_cores();

  // Rasterise one triangle. Assumes all clip.w > 0 (near-plane handling is the
  // caller's job — see pico3d_draw_mesh). Performs viewport map, back-face cull,
  // depth test, perspective-correct texture/colour, and writes colour+depth.
  // `light` is only used when m->normal_map is set (per-pixel lighting); pass
  // null otherwise. Returns the number of pixels written.
  int pico3d_raster_triangle(pico3d_target_t *t, const pico3d_tri_t *tri,
                             const pico3d_material_t *m, const pico3d_light_t *light);

}
