#pragma once

// pico3d — a small fixed-function 3D rasteriser for Badgeware.
//
// The engine is deliberately decoupled from picovector's image_t: it renders
// into plain pointer "views" (pico3d_target_t / pico3d_texture_t) so the hot loop
// builds and unit-tests on the host with no Pico SDK present. The MicroPython
// binding layer is the only place that knows about image_t — it fills these
// views from image_t::ptr()/bounds() and a caller-owned depth buffer.
//
// Colour format matches the Tufty framebuffer word: 0xAABBGGRR
//   (R = bits 0..7, G = 8..15, B = 16..23). See st7789.cpp ST_RGB565.
//
// Material, light and texture colours carry no alpha - the rasteriser neither
// reads nor blends it - but a written pixel is opaque, so a target that is not
// the framebuffer can be blitted or sampled afterwards. Leaving alpha at zero
// made every rendered pixel fully transparent to picovector.

#include <stdint.h>

#include "vec3.hpp"
#include "mat4.hpp"

namespace picovector {

  // --- colour helpers (0x00BBGGRR) -----------------------------------------
  static inline uint32_t pico3d_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
  }

  // Pack 0x00BBGGRR to RGB565 by truncation - bit-identical to picovector's
  // pv_8888_to_565 (image.hpp) and to the display scan-out conversion, which is
  // what makes a 565 render byte-comparable to a quantised RGBA one. Duplicated
  // here because this header stays free of the 2D library.
  static inline uint16_t pico3d_pack_565(uint32_t c) {
    return (uint16_t)(((c & 0xf8u) << 8) | ((c & 0xfc00u) >> 5) | ((c & 0xf80000u) >> 19));
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
    // The colour buffer is RGB565 (uint16 rows through the same pointer): the
    // platform framebuffer. The engine's interpolation is unchanged; only the
    // final store packs. Depth, clip and strides mean what they always did
    // (color_stride stays in PIXELS).
    bool color565;
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
  //
  // This is the largest entry. The engine packs entries only as long as a
  // draw's material needs - 24 bytes for UNLIT/GOURAUD, 36 for FLAT or matcap,
  // 48 when normal-mapped - so the cache is a run of bytes rather than an array
  // of these, and the fields every path reads come first. It lives in PSRAM on
  // device, where each entry is written once and read back per triangle, so
  // the fewer bytes a vertex touches, the faster the frame.
  struct pico3d_vcache_t {
    // In 8-byte PSRAM cache lines: [sx sy] is all the extent pass reads, [z rgb]
    // completes what an untextured, unfogged triangle needs, and [w nd] is read
    // only for 1/w (texture coordinates) or fog.
    float    sx, sy;  // pre-projected screen position (computed once here, not per triangle)
    float    z;       // depth (z/w)
    uint32_t rgb;     // per-vertex colour: final for UNLIT/GOURAUD, base for FLAT/normal-mapped
    // Written only when something will read them (texture coordinates, per-pixel
    // lighting, or fog on the target at add() time): without them an entry is
    // 16 bytes, two lines. See vcache_stride.
    float    w;       // clip-space w; 1/w is recomputed from it
    float    nd;      // distance in front of the near plane in clip space (z + w)
    // Only in the entries whose material needs them:
    //   FLAT:          ext[0] = world position (face normals)
    //   matcap:        ext[0] = normal mapped to matcap uv
    //   normal-mapped: ext[0] = world normal, ext[1] = world tangent
    vec3_t   ext[2];
  };

  // The rasteriser works its edge functions in exact 32-bit integers, which
  // holds for screen vertices within this many pixels of the target's origin
  // (see pico3d_raster_triangle). The draw stage clips anything reaching further
  // back to a guard band inside it.
  static constexpr int PICO3D_RASTER_LIMIT_PX = 1023;

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
  //
  // Every counter is per core ([0] core0, [1] core1): both cores run the raster,
  // and a shared counter bumped from both would lose updates.
#if defined(__arm__)
  // Which core this is, from the SIO CPUID register.
  static inline int pico3d_prof_core() { return (int)(*(volatile uint32_t *)0xD0000000u & 1u); }
  // Each core has its own DWT, so each has to enable its own counter. Out of
  // line (in flash): it runs once a core, and inlined it would cost SRAM at
  // every timing point in the SRAM-resident raster.
  void pico3d_prof_enable(int core);
  extern bool pico3d_prof_enabled[2];
  static inline uint32_t pico3d_prof_cyc() {
    int core = pico3d_prof_core();
    if (!pico3d_prof_enabled[core]) pico3d_prof_enable(core);
    return *(volatile uint32_t *)0xE0001004u;             //   DWT_CYCCNT
  }
  #define PICO3D_PROF 1
#else
  static inline int pico3d_prof_core() { return 0; }
  static inline uint32_t pico3d_prof_cyc() { return 0; }
  #define PICO3D_PROF 0
#endif
  #define PICO3D_PC pico3d_prof_core()
  extern uint64_t pico3d_prof_transform_cyc[2], pico3d_prof_build_cyc[2],
                  pico3d_prof_project_cyc[2], pico3d_prof_planes_cyc[2], pico3d_prof_edges_cyc[2],
                  pico3d_prof_fill_cyc[2], pico3d_prof_bbox_px[2], pico3d_prof_px[2];

  // The finer breakdown behind engine.profile_detail(), per core. Cycles unless
  // noted. XFORM is transform_range itself (TRANSFORM above is core0's wall time
  // for the whole pass, waiting on core1 included); PASS2 is all of draw_pass2,
  // RASTER the part of it inside pico3d_raster_triangle, so assembly is the
  // difference; WAIT is core0 idle in a join, waiting for core1 to finish.
  enum {
    PICO3D_PD_ADD_WALL, PICO3D_PD_XFORM, PICO3D_PD_EXTENTS,
    PICO3D_PD_DRAW_WALL, PICO3D_PD_CLEAR, PICO3D_PD_BIN, PICO3D_PD_PASS2,
    PICO3D_PD_RASTER, PICO3D_PD_WAIT,
    PICO3D_PD_VERTS, PICO3D_PD_TRIS_IN, PICO3D_PD_TRIS_DRAWN, PICO3D_PD_TRIS_CLIPPED,   // counts
    PICO3D_PD_ROWS, PICO3D_PD_ROWS_EMPTY, PICO3D_PD_FILLS, PICO3D_PD_SPAN_PX,       // XP counts
    PICO3D_PD_COUNT
  };
  extern uint32_t pico3d_prof_detail[2][PICO3D_PD_COUNT];   // 32-bit: read every second or so, wraps after 17 s
#if PICO3D_PROF
  #define PICO3D_PD_START(t) uint32_t t = pico3d_prof_cyc()
  #define PICO3D_PD_ADD(id, t) (pico3d_prof_detail[PICO3D_PC][id] += pico3d_prof_cyc() - (t))
  #define PICO3D_PD_COUNTN(id, n) (pico3d_prof_detail[PICO3D_PC][id] += (uint32_t)(n))
#else
  #define PICO3D_PD_START(t) ((void)0)
  #define PICO3D_PD_ADD(id, t) ((void)0)
  #define PICO3D_PD_COUNTN(id, n) ((void)0)
#endif

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
    uint32_t                 vbase;        // its vertices, from this BYTE offset in the arena
    uint32_t                 vstride;      // bytes a vertex entry takes (see pico3d_vcache_t)
    // The index list the bands read: the mesh's own, or a copy the scene cached
    // in fast memory (see pico3d_scene_t.cache_low).
    const uint16_t          *indices;
    mat4_t                   mvp;          // to re-project a triangle the near plane cuts
    uint32_t                 tbase;        // its triangles' screen-Y extents, from here
    // Scratch, refilled for each band: this submission's live triangles, as a
    // slice of a bin. [0] is core0's (or the only core's) slice of `bin`, [1]
    // core1's slice of `bin1` when the two cores draw a band's halves apart.
    uint32_t                 boff[2], bcount[2];
  };

  // Caller-owned storage. Every array is sized by the caller and never grown:
  // add() returns false rather than allocating, so a frame cannot stall on a
  // heap. `ys` holds two int16 a triangle (min and max screen row).
  struct pico3d_scene_t {
    pico3d_sub_t    *subs;   uint32_t sub_cap,  sub_count;
    pico3d_vcache_t *verts;  uint32_t vert_cap, vert_count;
    uint32_t         vert_bytes;                 // bytes of `verts` used (entries are packed)
    // Size of `verts` in bytes when the embedder set it (an SRAM pool region);
    // 0 keeps the historic bound of vert_cap worst-case entries.
    uint32_t         vert_arena_bytes;
    // Index copies cached in the tail of `verts`' region, growing DOWN from
    // vert_arena_bytes while the per-frame entries grow up: cache_low is the
    // watermark, cache_count the meshes cached so far (in `cached`). 0/empty
    // when the region is the plain heap, where a copy would gain nothing.
    uint32_t         cache_low, cache_count;
    struct { const pico3d_mesh_t *mesh; const uint16_t *copy; } cached[16];
    int16_t         *ys;     uint32_t tri_cap,  tri_count;
    uint16_t        *bin;    uint32_t bin_cap;   // scratch: one submission's live triangles
    // Optional second bin, bin_cap long, for core1. With it, two cores split each
    // band into a top and a bottom half and each bins, sets up and fills only its
    // own; without it they share `bin` and fill alternate rows, which sets up
    // every triangle twice.
    uint16_t        *bin1;
    float            tw, th;                     // viewport the vertices were projected for
    // Rows the scene's visible triangles touch, [ymin, ymax], from add(). A
    // banded draw bands only these, so empty rows above and below cost nothing
    // and the bands are spread over what is actually there.
    int32_t          ymin, ymax;
    int32_t          xmin, xmax;   // columns touched, same contract as ymin/ymax
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
  // of the scene's arrays is too small, having added nothing; `why`, when
  // given, names the array so a binding can raise something legible.
  bool pico3d_scene_add(pico3d_scene_t *sc, const pico3d_target_t *t,
                        const pico3d_mesh_t *mesh, const mat4_t *model,
                        const mat4_t *view_proj, const pico3d_material_t *material,
                        pico3d_shading_t shading, const pico3d_light_t *light,
                        const mat4_t *view = nullptr, const char **why = nullptr,
                        bool flat_depth = false);

  // Rasterise the scene into `t`, `band_rows` rows at a time (<= 0 means one
  // band covering the whole clip). When `t->depth` is only band_rows tall, pass
  // its height as band_rows and this will point depth_y0 at each band in turn
  // and clear it - so a small, fast depth buffer serves a whole screen. Banded,
  // it draws only the rows the scene touches, in as few bands as band_rows
  // allows, all of about equal height.
  // Returns the number of triangles rasterised.
  int pico3d_scene_draw(pico3d_scene_t *sc, pico3d_target_t *t, int band_rows,
                        uint16_t clear_to = 0xFFFF);

  // Rasterise the per-triangle pass on both cores (n=2, top/bottom screen bands) or
  // the calling core only (n=1, default). Geometry/setup is duplicated per core, so
  // the win is on FILL-bound scenes. No-op (always 1 core) on host.
  void pico3d_set_cores(int n);
  int  pico3d_get_cores();
  // The fewest bands a two-core banded draw cuts the scene into, so the two
  // cores have work to share out: more balances better but bins more often.
  void pico3d_set_work_bands(int n);
  int  pico3d_get_work_bands();

  // Rasterise one triangle. Assumes all clip.w > 0 (near-plane handling is the
  // caller's job — see pico3d_draw_mesh). Performs viewport map, back-face cull,
  // depth test, perspective-correct texture/colour, and writes colour+depth.
  // `light` is only used when m->normal_map is set (per-pixel lighting); pass
  // null otherwise. Returns the number of pixels written.
  int pico3d_raster_triangle(pico3d_target_t *t, const pico3d_tri_t *tri,
                             const pico3d_material_t *m, const pico3d_light_t *light);

}
