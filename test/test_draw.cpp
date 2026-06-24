// Host test for the mesh draw stage (transform + shading).
//   c++ -std=c++17 -I.. test_draw.cpp ../pico3d_raster.cpp ../pico3d_draw.cpp -o test_draw && ./test_draw

#include <cstdio>
#include <vector>

#include "pico3d.hpp"

using namespace picovector;

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)){ failures++; printf("FAIL %s\n", msg);} else printf("ok   %s\n", msg);} while(0)

static const int W = 64, H = 64;
static std::vector<uint32_t> color;
static std::vector<uint16_t> depth;
static pico3d_vcache_t VC[64];   // vertex transform scratch for tests

static pico3d_target_t make_target() {
  color.assign(W * H, 0);
  depth.assign(W * H, 0xFFFF);
  pico3d_target_t t{};
  t.color = color.data(); t.depth = depth.data();
  t.width = W; t.height = H; t.color_stride = W; t.depth_stride = W;
  t.clip_x0 = 0; t.clip_y0 = 0; t.clip_x1 = W; t.clip_y1 = H;
  return t;
}

// unit quad in the z=0 plane, facing +z
static const float QPOS[] = {
  -1,-1,0,   1,-1,0,   1,1,0,   -1,1,0,
};
static const float QNRM[] = {
  0,0,1,  0,0,1,  0,0,1,  0,0,1,
};
static const float QUV[] = { 0,1,  1,1,  1,0,  0,0 };
static const uint16_t QIDX[] = { 0,1,2, 0,2,3 };

static pico3d_mesh_t quad() {
  pico3d_mesh_t m{};
  m.positions = QPOS; m.normals = QNRM; m.uvs = QUV; m.indices = QIDX;
  m.vertex_count = 4; m.triangle_count = 2;
  return m;
}

static int brightness(uint32_t c) { return (c & 0xff) + ((c >> 8) & 0xff) + ((c >> 16) & 0xff); }

int main() {
  pico3d_mesh_t m = quad();
  mat4_t model;                                  // identity
  mat4_t vp; vp.perspective(60, 1.0f, 0.1f, 10.0f).look_at(vec3_t(0,0,3), vec3_t(0,0,0), vec3_t(0,1,0));

  int center = (H/2) * W + (W/2);

  // 1) UNLIT: centre pixel is exactly the material colour.
  {
    pico3d_target_t t = make_target();
    pico3d_material_t mat{}; mat.color = pico3d_rgb(200, 100, 50); mat.filter = PICO3D_NEAREST;
    int n = pico3d_draw_mesh(&t, &m, &model, &vp, &mat, PICO3D_UNLIT, nullptr, VC);
    CHECK(n == 2, "unlit: both triangles drawn");
    CHECK(color[center] == pico3d_rgb(200,100,50), "unlit: centre == material colour");
  }

  // 2) GOURAUD lit head-on is bright; light facing away leaves only ambient.
  {
    pico3d_material_t mat{}; mat.color = pico3d_rgb(255,255,255); mat.filter = PICO3D_NEAREST; mat.double_sided = true;
    pico3d_light_t lit{}; lit.color = pico3d_rgb(255,255,255); lit.ambient = pico3d_rgb(20,20,20);

    pico3d_target_t t1 = make_target();
    lit.direction = vec3_t(0,0,-1);              // shining onto +z face
    pico3d_draw_mesh(&t1, &m, &model, &vp, &mat, PICO3D_GOURAUD, &lit, VC);
    int lit_b = brightness(color[center]);

    pico3d_target_t t2 = make_target();
    lit.direction = vec3_t(0,0,1);               // shining away
    pico3d_draw_mesh(&t2, &m, &model, &vp, &mat, PICO3D_GOURAUD, &lit, VC);
    int dark_b = brightness(color[center]);

    CHECK(lit_b > 700, "gouraud: head-on is bright");
    CHECK(dark_b < 100 && dark_b > 0, "gouraud: facing away leaves ambient only");
    CHECK(lit_b > dark_b, "gouraud: lit brighter than ambient");
  }

  // 3) FLAT shading produces a single uniform colour across the face.
  {
    pico3d_target_t t = make_target();
    pico3d_material_t mat{}; mat.color = pico3d_rgb(255,255,255); mat.double_sided = true;
    pico3d_light_t lit{}; lit.color = pico3d_rgb(255,255,255); lit.ambient = pico3d_rgb(30,30,30);
    lit.direction = vec3_t(0,0,-1);
    pico3d_draw_mesh(&t, &m, &model, &vp, &mat, PICO3D_FLAT, &lit, VC);
    uint32_t a = color[center];
    uint32_t b = color[center + 5];              // another interior pixel
    CHECK(a != 0 && a == b, "flat: face colour is uniform");
  }

  // 4) Texture modulates the lit colour.
  {
    pico3d_target_t t = make_target();
    uint32_t tex[4] = { pico3d_rgb(255,0,0), pico3d_rgb(255,0,0), pico3d_rgb(255,0,0), pico3d_rgb(255,0,0) };
    pico3d_texture_t texture{ tex, 2, 2 };
    pico3d_material_t mat{}; mat.color = pico3d_rgb(255,255,255); mat.texture = &texture; mat.double_sided = true;
    pico3d_draw_mesh(&t, &m, &model, &vp, &mat, PICO3D_UNLIT, nullptr, VC);
    uint32_t c = color[center];
    CHECK((c & 0xff) > 200 && ((c >> 8) & 0xff) == 0 && ((c >> 16) & 0xff) == 0, "texture: red texel applied");
  }

  // 5) alpha cutout: a transparent texel (alpha < cutoff) writes neither colour
  //    nor depth; an opaque texel with the same cutoff draws normally.
  {
    pico3d_target_t t = make_target();
    uint32_t clear_tex[1] = { 0x00000000u };            // alpha 0
    pico3d_texture_t texture{ clear_tex, 1, 1 };
    pico3d_material_t mat{};
    mat.color = pico3d_rgb(255,255,255); mat.texture = &texture;
    mat.double_sided = true; mat.alpha_cutoff = 128;
    pico3d_draw_mesh(&t, &m, &model, &vp, &mat, PICO3D_UNLIT, nullptr, VC);
    CHECK(color[center] == 0, "alpha cutout: transparent texel discarded (no colour)");
    CHECK(depth[center] == 0xFFFF, "alpha cutout: transparent texel writes no depth");

    pico3d_target_t t2 = make_target();
    uint32_t op_tex[1] = { 0xFF000000u | pico3d_rgb(0,255,0) };  // opaque green
    pico3d_texture_t texture2{ op_tex, 1, 1 };
    pico3d_material_t mat2 = mat; mat2.texture = &texture2;
    pico3d_draw_mesh(&t2, &m, &model, &vp, &mat2, PICO3D_UNLIT, nullptr, VC);
    CHECK(((color[center] >> 8) & 0xff) > 200, "alpha cutout: opaque texel drawn");
  }

  // 6) per-vertex colours override material colour (the flat-facet path).
  {
    pico3d_target_t t = make_target();
    uint32_t cols[4] = { pico3d_rgb(20,200,40), pico3d_rgb(20,200,40),
                         pico3d_rgb(20,200,40), pico3d_rgb(20,200,40) };
    pico3d_mesh_t mc = m;            // same quad, now with vertex colours
    mc.colors = cols;
    pico3d_material_t mat{}; mat.color = pico3d_rgb(255,0,0);  // must be ignored
    pico3d_draw_mesh(&t, &mc, &model, &vp, &mat, PICO3D_UNLIT, nullptr, VC);
    CHECK(color[center] == pico3d_rgb(20,200,40), "vertex colours override material colour");

    // flat-shaded vertex colour is modulated by light, not replaced by material
    pico3d_target_t t2 = make_target();
    pico3d_light_t lit{}; lit.color = pico3d_rgb(255,255,255); lit.ambient = pico3d_rgb(0,0,0);
    lit.direction = vec3_t(0,0,-1);
    pico3d_draw_mesh(&t2, &mc, &model, &vp, &mat, PICO3D_FLAT, &lit, VC);
    uint32_t c = color[center];
    CHECK((c & 0xff) < 60 && ((c>>8)&0xff) > 120 && ((c>>16)&0xff) < 80,
          "flat shading keeps the vertex hue (lit green)");
  }

  // 7) matcap (env map): sampled by the surface normal, modulates the base colour.
  {
    // a flat env: with a white base the centre takes the matcap colour straight.
    uint32_t mcflat[4] = { pico3d_rgb(100,150,200), pico3d_rgb(100,150,200),
                           pico3d_rgb(100,150,200), pico3d_rgb(100,150,200) };
    pico3d_texture_t mc{}; mc.texels = mcflat; mc.width = 2; mc.height = 2;
    pico3d_material_t mat{}; mat.color = pico3d_rgb(255,255,255);
    mat.filter = PICO3D_NEAREST; mat.matcap = &mc;
    pico3d_target_t t = make_target();
    pico3d_draw_mesh(&t, &m, &model, &vp, &mat, PICO3D_UNLIT, nullptr, VC);
    CHECK(color[center] == pico3d_rgb(100,150,200), "matcap: flat env modulates base colour");

    // a +x gradient env: the sample tracks the normal, so rotating the quad about
    // Y (tilting its normal) must change the sampled red.
    const int MS = 8; uint32_t grad[MS*MS];
    for (int y = 0; y < MS; y++) for (int x = 0; x < MS; x++) grad[y*MS+x] = pico3d_rgb(x*32, 0, 0);
    pico3d_texture_t mg{}; mg.texels = grad; mg.width = MS; mg.height = MS;
    pico3d_material_t mat2{}; mat2.color = pico3d_rgb(255,255,255);
    mat2.filter = PICO3D_NEAREST; mat2.matcap = &mg; mat2.double_sided = true;

    pico3d_target_t ta = make_target();
    pico3d_draw_mesh(&ta, &m, &model, &vp, &mat2, PICO3D_UNLIT, nullptr, VC);
    int red_face = color[center] & 0xff;     // normal +z -> uv.x ~0.5

    mat4_t rot; rot.rotate_y(35.0f);         // tilt the normal toward +/-x (degrees)
    pico3d_target_t tb = make_target();
    pico3d_draw_mesh(&tb, &m, &rot, &vp, &mat2, PICO3D_UNLIT, nullptr, VC);
    int red_rot = color[center] & 0xff;
    CHECK(red_face != red_rot, "matcap: rotating the surface sweeps the env sample");
  }

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures==1?"":"s");
  return failures ? 1 : 0;
}
