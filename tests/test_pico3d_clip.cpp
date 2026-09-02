// Near-plane clipping in the mesh draw stage.
//
// A triangle with a vertex behind the eye used to be dropped whole, so anything
// you walked into vanished: the floor underfoot, the wall beside you. The draw
// stage now cuts such a triangle against the eye plane and re-projects the
// pieces.
//
// The assertions are about coverage rather than exact pixels: geometry that
// spans the camera has to paint, has to stay inside its own half of the frame,
// and has to agree with the same geometry moved wholly in front of the eye.

#include "test.hpp"
#include "pico3d.hpp"

using namespace picovector;

namespace {

  const int W = 64, H = 64;
  uint32_t colour[W * H];
  uint16_t depth[W * H];
  pico3d_vcache_t cache[16];

  pico3d_target_t target() {
    for (int i = 0; i < W * H; i++) { colour[i] = 0; depth[i] = 0xFFFF; }
    pico3d_target_t t{};
    t.color = colour; t.depth = depth;
    t.width = W; t.height = H; t.color_stride = W; t.depth_stride = W;
    t.clip_x0 = 0; t.clip_y0 = 0; t.clip_x1 = W; t.clip_y1 = H;
    return t;
  }

  int painted(int y0, int y1) {
    int n = 0;
    for (int y = y0; y < y1; y++)
      for (int x = 0; x < W; x++)
        if (colour[y * W + x]) n++;
    return n;
  }

  // A horizontal slab at height y, from z0 to z1, one unit either side of x.
  int draw_slab(float y, float z0, float z1) {
    const float pos[] = { -1, y, z0,  1, y, z0,  1, y, z1,  -1, y, z1 };
    const uint16_t idx[] = { 0, 1, 2, 0, 2, 3 };
    pico3d_mesh_t mesh{};
    mesh.positions = pos; mesh.indices = idx;
    mesh.vertex_count = 4; mesh.triangle_count = 2;

    pico3d_material_t m{};
    m.color = pico3d_rgb(255, 255, 255);
    m.double_sided = true;                 // the test is the clip, not the winding

    mat4_t model;                          // identity
    mat4_t view; view.look_at(vec3_t(0, 0, 0), vec3_t(0, 0, -1), vec3_t(0, 1, 0));
    mat4_t vp; vp.perspective(70.0f, 1.0f, 0.1f, 50.0f); vp.multiply(view);

    pico3d_target_t t = target();
    return pico3d_draw_mesh(&t, &mesh, &model, &vp, &m, PICO3D_UNLIT, nullptr, cache);
  }

}

void test_pico3d_clip() {
  printf("clip: a surface the camera stands on still draws\n");
  {
    // Spans z = +2 (behind the eye) to z = -8 (ahead), so both triangles have a
    // vertex behind the eye plane. This is the floor case, and the whole thing
    // used to disappear.
    int drawn = draw_slab(-0.5f, 2.0f, -8.0f);
    CHECK(drawn > 0);
    CHECK(painted(H / 2, H) > 200);
    // A floor cannot appear above the horizon, which for a level camera is the
    // middle row. Getting the clip interpolation wrong throws vertices there.
    CHECK(painted(0, H / 2) == 0);
  }

  printf("clip: cutting at the eye covers more than starting ahead of it\n");
  {
    // The clip plane sits at the eye, not at the near plane, so a slab cut there
    // reaches further down the frame than one that begins a unit in front. Both
    // stay under the horizon; what matters is that the cut one is the larger.
    draw_slab(-0.5f, -1.0f, -8.0f);
    int ahead = painted(0, H);
    draw_slab(-0.5f, 2.0f, -8.0f);
    int across = painted(0, H);
    CHECK(ahead > 0);
    CHECK(across >= ahead);
    printf("       ahead %d px, cut at the eye %d px\n", ahead, across);
  }

  printf("clip: geometry entirely behind the eye paints nothing\n");
  {
    int drawn = draw_slab(-0.5f, 8.0f, 2.0f);
    CHECK(drawn == 0);
    CHECK(painted(0, H) == 0);
  }

  printf("clip: a wall alongside the camera keeps its near end\n");
  {
    // A vertical quad running past the eye on the left, from behind to well
    // ahead - the maze wall that used to pop out of existence.
    const float pos[] = { -0.5f, -0.5f, 3,  -0.5f, -0.5f, -6,
                          -0.5f,  0.5f, -6, -0.5f,  0.5f, 3 };
    const uint16_t idx[] = { 0, 1, 2, 0, 2, 3 };
    pico3d_mesh_t mesh{};
    mesh.positions = pos; mesh.indices = idx;
    mesh.vertex_count = 4; mesh.triangle_count = 2;
    pico3d_material_t m{};
    m.color = pico3d_rgb(255, 255, 255); m.double_sided = true;
    mat4_t model, view, vp;
    view.look_at(vec3_t(0, 0, 0), vec3_t(0, 0, -1), vec3_t(0, 1, 0));
    vp.perspective(70.0f, 1.0f, 0.1f, 50.0f); vp.multiply(view);
    pico3d_target_t t = target();
    CHECK(pico3d_draw_mesh(&t, &mesh, &model, &vp, &m, PICO3D_UNLIT, nullptr, cache) > 0);
    // It is to the left of the camera, so it paints the left of the frame and
    // reaches the edge - that is the near end the drop used to take away.
    int left = 0, edge = 0;
    for (int y = 0; y < H; y++) {
      for (int x = 0; x < W / 2; x++) if (colour[y * W + x]) left++;
      if (colour[y * W]) edge++;
    }
    CHECK(left > 100);
    CHECK(edge > 0);
  }
}
