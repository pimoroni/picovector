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
