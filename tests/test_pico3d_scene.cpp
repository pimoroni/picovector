// Host tests for the deferred scene and banded rendering.
//
// The property that matters: rendering a scene in bands, against a depth buffer
// only as tall as one band, must be PIXEL-IDENTICAL to rendering it in one pass
// against a full-screen depth buffer. If that holds, banding is purely a memory
// strategy and cannot change what you see - which is the whole point, since it
// exists so the depth buffer can live in fast memory too small to hold a screen.

#include "test.hpp"

#include <cstdio>
#include <vector>

#include "pico3d.hpp"

using namespace picovector;

namespace {

const int W = 64, H = 64;

// Four overlapping quads at different depths, so the depth test genuinely
// decides the image and any band seam or row-offset slip would show.
struct tquad_t {
  float pos[12];
  uint32_t col[4];
};

tquad_t make_quad(float cx, float cy, float z, float r, uint32_t c) {
  tquad_t q{};
  const float p[12] = { cx - r, cy - r, z,  cx + r, cy - r, z,
                        cx + r, cy + r, z,  cx - r, cy + r, z };
  for (int i = 0; i < 12; i++) q.pos[i] = p[i];
  for (int i = 0; i < 4; i++) q.col[i] = c;
  return q;
}

const uint16_t QIDX[] = { 0, 1, 2, 0, 2, 3 };

pico3d_target_t target_for(std::vector<uint32_t> &color, uint16_t *depth,
                           int depth_rows) {
  color.assign((size_t)W * H, 0u);
  pico3d_target_t t{};
  t.color = color.data();
  t.depth = depth;
  t.width = W; t.height = H;
  t.color_stride = W;
  t.depth_stride = W;
  t.clip_x0 = 0; t.clip_y0 = 0; t.clip_x1 = W; t.clip_y1 = H;
  (void)depth_rows;
  return t;
}

}  // namespace

void test_pico3d_scene() {
  // A spread of depths and positions, deliberately overlapping.
  tquad_t quads[] = {
    make_quad(-0.3f,  0.1f, -6.0f, 1.1f, pico3d_rgb(200,  40,  40)),
    make_quad( 0.4f, -0.2f, -5.0f, 0.9f, pico3d_rgb( 40, 200,  40)),
    make_quad( 0.0f,  0.3f, -4.0f, 0.7f, pico3d_rgb( 40,  40, 200)),
    make_quad(-0.2f, -0.4f, -3.0f, 0.5f, pico3d_rgb(200, 200,  40)),
  };
  constexpr int NQ = 4;

  pico3d_mesh_t meshes[NQ];
  for (int i = 0; i < NQ; i++) {
    pico3d_mesh_t m{};
    m.positions = quads[i].pos; m.colors = quads[i].col; m.indices = QIDX;
    m.vertex_count = 4; m.triangle_count = 2;
    meshes[i] = m;
  }

  pico3d_material_t mat{};
  mat.color = pico3d_rgb(255, 255, 255);
  mat.filter = PICO3D_NEAREST;

  mat4_t model;
  mat4_t proj; proj.perspective(60.0f, 1.0f, 1.0f, 50.0f);

  // --- A: the immediate path, one full-screen depth buffer -------------------
  std::vector<uint32_t> ca;
  std::vector<uint16_t> da((size_t)W * H, 0xFFFF);
  pico3d_vcache_t vc[4];
  {
    pico3d_target_t t = target_for(ca, da.data(), H);
    pico3d_depth_clear(&t, 0xFFFF);
    for (int i = 0; i < NQ; i++)
      pico3d_draw_mesh(&t, &meshes[i], &model, &proj, &mat, PICO3D_UNLIT,
                       nullptr, vc, nullptr);
  }

  // --- the same geometry, as a scene ----------------------------------------
  std::vector<pico3d_sub_t> subs(NQ);
  std::vector<pico3d_vcache_t> verts((size_t)NQ * 4);
  std::vector<int16_t> ys((size_t)NQ * 2 * 2);
  std::vector<uint16_t> bin(8);
  pico3d_scene_t sc{};
  sc.subs = subs.data();   sc.sub_cap = (uint32_t)subs.size();
  sc.verts = verts.data(); sc.vert_cap = (uint32_t)verts.size();
  sc.ys = ys.data();       sc.tri_cap = (uint32_t)(ys.size() / 2);
  sc.bin = bin.data();     sc.bin_cap = (uint32_t)bin.size();

  auto build = [&](pico3d_target_t *t) {
    pico3d_scene_reset(&sc);
    for (int i = 0; i < NQ; i++)
      CHECK_MSG(pico3d_scene_add(&sc, t, &meshes[i], &model, &proj, &mat,
                                 PICO3D_UNLIT, nullptr, nullptr),
                "scene: add fits in a scene sized for it");
  };

  // --- B: one band, full-screen depth buffer --------------------------------
  std::vector<uint32_t> cb;
  std::vector<uint16_t> db((size_t)W * H, 0xFFFF);
  {
    pico3d_target_t t = target_for(cb, db.data(), H);
    build(&t);
    pico3d_scene_draw(&sc, &t, 0);
  }
  // Guard against the comparisons below passing vacuously: the reference has to
  // be a real image, with all four quads visible and depth actually resolving
  // their overlaps.
  {
    int painted = 0;
    bool seen[NQ] = {false, false, false, false};
    for (uint32_t px : ca) {
      if (px) painted++;
      for (int i = 0; i < NQ; i++) if (px == quads[i].col[0]) seen[i] = true;
    }
    CHECK_MSG(painted > W * H / 8, "scene: the reference image is substantially painted");
    CHECK_MSG(seen[0] && seen[1] && seen[2] && seen[3],
              "scene: every quad survives the depth test somewhere");
  }

  CHECK_MSG(ca == cb, "scene: one unbanded pass matches the immediate path");

  // --- C: banded, against a depth buffer only one band tall -----------------
  // The interesting case: 64 rows drawn against a 16-row buffer, four times
  // over, with depth_y0 walked down the screen.
  for (int band : {1, 3, 8, 16, 32, 63}) {
    std::vector<uint32_t> cc;
    std::vector<uint16_t> dc((size_t)W * band, 0x0000);   // deliberately not pre-cleared
    pico3d_target_t t = target_for(cc, dc.data(), band);
    build(&t);
    pico3d_scene_draw(&sc, &t, band);
    char msg[96];
    snprintf(msg, sizeof(msg),
             "scene: %d-row bands against a %d-row depth buffer match one pass", band, band);
    CHECK_MSG(ca == cc, msg);
  }

  // --- a band taller than the screen is just one band -----------------------
  {
    std::vector<uint32_t> cd;
    std::vector<uint16_t> dd((size_t)W * H, 0xFFFF);
    pico3d_target_t t = target_for(cd, dd.data(), H);
    build(&t);
    pico3d_scene_draw(&sc, &t, H * 4);
    CHECK_MSG(ca == cd, "scene: a band bigger than the clip degenerates to one pass");
  }

  // --- with no depth buffer at all, banding must still not change anything --
  {
    std::vector<uint32_t> c1, c2;
    pico3d_target_t t1 = target_for(c1, nullptr, 0);
    build(&t1);
    pico3d_scene_draw(&sc, &t1, 0);
    pico3d_target_t t2 = target_for(c2, nullptr, 0);
    build(&t2);
    pico3d_scene_draw(&sc, &t2, 7);
    CHECK_MSG(c1 == c2, "scene: depth-free banding paints the same as one pass");
  }

  // --- the scene refuses to overflow rather than allocating -----------------
  {
    pico3d_scene_t tiny{};
    pico3d_sub_t one_sub{};
    pico3d_vcache_t two_verts[2];
    int16_t one_tri[2];
    uint16_t one_bin[1];
    tiny.subs = &one_sub;      tiny.sub_cap = 1;
    tiny.verts = two_verts;    tiny.vert_cap = 2;
    tiny.ys = one_tri;         tiny.tri_cap = 1;
    tiny.bin = one_bin;        tiny.bin_cap = 1;
    pico3d_scene_reset(&tiny);
    std::vector<uint32_t> cx;
    pico3d_target_t t = target_for(cx, nullptr, 0);
    CHECK_MSG(!pico3d_scene_add(&tiny, &t, &meshes[0], &model, &proj, &mat,
                                PICO3D_UNLIT, nullptr, nullptr),
              "scene: a mesh too big for the arena is refused, not allocated");
    CHECK_MSG(tiny.sub_count == 0, "scene: a refused add leaves the scene untouched");
  }

  // --- the parity row split ------------------------------------------------
  // This is how two cores share one band: same geometry, opposite row_phase.
  // Filling both halves separately must equal filling every row in one pass,
  // or the split is not a memory strategy but a rendering change.
  {
    std::vector<uint32_t> whole, halves;
    std::vector<uint16_t> dw((size_t)W * H, 0xFFFF), dh((size_t)W * H, 0xFFFF);

    pico3d_target_t t = target_for(whole, dw.data(), H);
    build(&t);
    pico3d_scene_draw(&sc, &t, 0);

    // even rows, then odd rows, into one image - as the two cores would
    pico3d_target_t t0 = target_for(halves, dh.data(), H);
    t0.row_step = 2; t0.row_phase = 0;
    build(&t0);
    pico3d_scene_draw(&sc, &t0, 0);
    pico3d_target_t t1 = t0;              // same colour + depth buffers
    t1.row_step = 2; t1.row_phase = 1;
    pico3d_scene_draw(&sc, &t1, 0);

    CHECK_MSG(whole == halves, "scene: two row phases together equal one full pass");

    // And each phase alone must paint only its own rows, or the cores overlap.
    std::vector<uint32_t> even;
    std::vector<uint16_t> de((size_t)W * H, 0xFFFF);
    pico3d_target_t te = target_for(even, de.data(), H);
    te.row_step = 2; te.row_phase = 0;
    build(&te);
    pico3d_scene_draw(&sc, &te, 0);
    bool odd_clean = true, even_painted = false;
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        uint32_t px = even[(size_t)y * W + x];
        if (y & 1) { if (px) odd_clean = false; }
        else if (px) even_painted = true;
      }
    CHECK_MSG(odd_clean, "scene: row_phase 0 leaves the odd rows untouched");
    CHECK_MSG(even_painted, "scene: row_phase 0 does paint the even rows");

    // Banding and the row split composed - the configuration the device runs.
    // Driven band by band, exactly as the dual-core path does it: each band
    // clears its strip once, then both phases fill it before moving on. Note
    // depth_y0 has to follow the band, since the strip is only 16 rows.
    std::vector<uint32_t> banded_split;
    std::vector<uint16_t> dbs((size_t)W * 16, 0xFFFF);
    pico3d_target_t b0 = target_for(banded_split, dbs.data(), 16);
    build(&b0);
    for (int by = 0; by < H; by += 16) {
      pico3d_target_t bt = b0;
      bt.clip_y0 = by; bt.clip_y1 = by + 16;
      bt.depth_y0 = by;                    // the strip covers THIS band only
      bt.row_step = 2; bt.row_phase = by;  // one core's half...
      pico3d_scene_draw(&sc, &bt, 0);      // 0: this call is already one band
      bt.row_phase = by + 1;               // ...then the other's
      bt.depth_y0 = by;
      pico3d_scene_draw(&sc, &bt, 0);
    }
    CHECK_MSG(whole == banded_split,
              "scene: banded AND row-split still equals one full pass");
  }

  // --- whole-mesh frustum culling -------------------------------------------
  {
    // bounds come off the positions
    tquad_t q = make_quad(1.0f, -2.0f, -7.0f, 0.5f, pico3d_rgb(255, 255, 255));
    pico3d_mesh_t m{};
    m.positions = q.pos; m.colors = q.col; m.indices = QIDX;
    m.vertex_count = 4; m.triangle_count = 2;
    CHECK_MSG(!pico3d_cull_mesh(&m, &proj), "cull: a mesh with no bounds is never culled");
    pico3d_mesh_bounds(&m);
    CHECK_MSG(m.has_bounds, "cull: bounds get computed");
    CHECK_MSG(m.bmin[0] == 0.5f && m.bmax[0] == 1.5f, "cull: bounds span x correctly");
    CHECK_MSG(m.bmin[1] == -2.5f && m.bmax[1] == -1.5f, "cull: bounds span y correctly");
    CHECK_MSG(m.bmin[2] == -7.0f && m.bmax[2] == -7.0f, "cull: a flat box is still bounded");

    mat4_t at_origin;
    // in view: dead ahead at a sane depth
    tquad_t inview = make_quad(0.0f, 0.0f, -6.0f, 0.5f, 0xffffff);
    pico3d_mesh_t mi{};
    mi.positions = inview.pos; mi.colors = inview.col; mi.indices = QIDX;
    mi.vertex_count = 4; mi.triangle_count = 2;
    pico3d_mesh_bounds(&mi);
    CHECK_MSG(!pico3d_cull_mesh(&mi, &proj), "cull: a mesh in front of the camera survives");

    // far off to one side, behind, and beyond the far plane
    struct { float x, y, z; const char *what; } outside[] = {
      {  60.0f,   0.0f,  -6.0f, "cull: far off to the right is culled" },
      { -60.0f,   0.0f,  -6.0f, "cull: far off to the left is culled" },
      {   0.0f,  60.0f,  -6.0f, "cull: far above is culled" },
      {   0.0f, -60.0f,  -6.0f, "cull: far below is culled" },
      {   0.0f,   0.0f,  20.0f, "cull: behind the camera is culled" },
      {   0.0f,   0.0f, -400.0f, "cull: beyond the far plane is culled" },
    };
    for (auto &o : outside) {
      tquad_t oq = make_quad(o.x, o.y, o.z, 0.5f, 0xffffff);
      pico3d_mesh_t mo{};
      mo.positions = oq.pos; mo.colors = oq.col; mo.indices = QIDX;
      mo.vertex_count = 4; mo.triangle_count = 2;
      pico3d_mesh_bounds(&mo);
      CHECK_MSG(pico3d_cull_mesh(&mo, &proj), o.what);
    }

    // A mesh straddling the edge must NOT be culled, or geometry pops.
    // Walk one across the right-hand edge and check nothing is dropped while
    // any part of it still lands on screen.
    bool kept_all_visible = true;
    for (float x = 0.0f; x < 12.0f; x += 0.25f) {
      tquad_t sq = make_quad(x, 0.0f, -6.0f, 0.5f, 0xffffff);
      pico3d_mesh_t ms{};
      ms.positions = sq.pos; ms.colors = sq.col; ms.indices = QIDX;
      ms.vertex_count = 4; ms.triangle_count = 2;
      pico3d_mesh_bounds(&ms);
      // does it actually paint anything?
      std::vector<uint32_t> px;
      std::vector<uint16_t> dz((size_t)W * H, 0xFFFF);
      pico3d_target_t t = target_for(px, dz.data(), H);
      pico3d_vcache_t v4[4];
      pico3d_draw_mesh(&t, &ms, &at_origin, &proj, &mat, PICO3D_UNLIT, nullptr, v4, nullptr);
      bool painted = false;
      for (uint32_t c : px) if (c) { painted = true; break; }
      if (painted && pico3d_cull_mesh(&ms, &proj)) kept_all_visible = false;
    }
    CHECK_MSG(kept_all_visible, "cull: nothing that paints a pixel is ever culled");

    // And culling must not change the image: adding an off-screen mesh to a
    // scene has to leave it byte-identical to not adding it at all.
    std::vector<uint32_t> without, with_offscreen;
    std::vector<uint16_t> d1((size_t)W * H, 0xFFFF), d2((size_t)W * H, 0xFFFF);
    pico3d_target_t t1 = target_for(without, d1.data(), H);
    build(&t1);
    pico3d_scene_draw(&sc, &t1, 0);

    tquad_t off = make_quad(80.0f, 0.0f, -6.0f, 2.0f, pico3d_rgb(255, 0, 255));
    pico3d_mesh_t mof{};
    mof.positions = off.pos; mof.colors = off.col; mof.indices = QIDX;
    mof.vertex_count = 4; mof.triangle_count = 2;
    pico3d_mesh_bounds(&mof);
    pico3d_target_t t2 = target_for(with_offscreen, d2.data(), H);
    build(&t2);
    uint32_t before = sc.sub_count;
    bool ok = pico3d_scene_add(&sc, &t2, &mof, &at_origin, &proj, &mat,
                               PICO3D_UNLIT, nullptr, nullptr);
    CHECK_MSG(ok, "cull: adding a culled mesh reports success, not failure");
    CHECK_MSG(sc.sub_count == before, "cull: a culled mesh takes no room in the scene");
    pico3d_scene_draw(&sc, &t2, 0);
    CHECK_MSG(without == with_offscreen, "cull: culling cannot change the image");
  }

  // --- the row extents actually exclude work --------------------------------
  // A quad in the top-left must be skipped entirely by a band across the
  // bottom, which is what makes banding cheap rather than N times the cost.
  {
    std::vector<uint32_t> cz;
    std::vector<uint16_t> dz((size_t)W * 8, 0xFFFF);
    pico3d_target_t t = target_for(cz, dz.data(), 8);
    pico3d_scene_reset(&sc);
    tquad_t q = make_quad(-0.6f, 0.6f, -4.0f, 0.3f, pico3d_rgb(255, 0, 0));
    pico3d_mesh_t m{};
    m.positions = q.pos; m.colors = q.col; m.indices = QIDX;
    m.vertex_count = 4; m.triangle_count = 2;
    pico3d_scene_add(&sc, &t, &m, &model, &proj, &mat, PICO3D_UNLIT, nullptr, nullptr);
    int16_t lo = sc.ys[0], hi = sc.ys[1];
    CHECK_MSG(lo >= 0 && hi < H, "scene: an on-screen triangle gets a bounded row extent");
    CHECK_MSG(hi < H - 8, "scene: a quad up the screen does not reach the bottom band");
  }
}
