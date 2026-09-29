// Quantised mesh positions: the transform and clipper reconstruct the same
// values, so a quantised mesh renders where the float one does. Quantisation
// moves vertices by at most half a step of bbox/65534 - far below a pixel
// here - but a moved vertex can flip an edge tie, so the images must agree
// almost everywhere rather than exactly.

#include "test.hpp"

#include <cstring>
#include <vector>

#include "pico3d.hpp"

using namespace picovector;

void test_pico3d_quant() {
  static const int W = 96, H = 96;
  // an octahedron: simple, closed, spans the axes unevenly
  static const float pos[] = { 1.1f,0,0, -1.1f,0,0, 0,0.7f,0, 0,-0.7f,0, 0,0,0.9f, 0,0,-0.9f };
  static const uint16_t idx[] = { 0,2,4, 2,1,4, 1,3,4, 3,0,4, 2,0,5, 1,2,5, 3,1,5, 0,3,5 };
  static pico3d_vcache_t vc[6];

  pico3d_mesh_t mf{};
  mf.positions = pos; mf.vertex_count = 6;
  mf.indices = idx; mf.triangle_count = 8;
  pico3d_mesh_bounds(&mf);

  mat4_t model;
  mat4_t vp; vp.perspective(40, 1.0f, 0.5f, 10.0f).look_at(vec3_t(0,0,3), vec3_t(0,0,0), vec3_t(0,1,0));
  pico3d_material_t mat{}; mat.color = pico3d_rgb(200, 128, 64);

  auto render = [&](const pico3d_mesh_t *mesh, std::vector<uint32_t> &fb) {
    fb.assign(W * H, 0);
    std::vector<uint16_t> depth(W * H, 0xFFFF);
    pico3d_target_t t{};
    t.color = fb.data(); t.depth = depth.data();
    t.width = W; t.height = H; t.color_stride = W; t.depth_stride = W;
    t.clip_x0 = 0; t.clip_y0 = 0; t.clip_x1 = W; t.clip_y1 = H;
    return pico3d_draw_mesh(&t, mesh, &model, &vp, &mat, PICO3D_UNLIT, nullptr, vc);
  };

  std::vector<uint32_t> fb_f, fb_q;
  int drawn_f = render(&mf, fb_f);

  pico3d_mesh_t mq = mf;
  int16_t q[6 * 3];
  pico3d_mesh_quantise(&mq, q);
  CHECK_MSG(mq.positions_q == q, "quantise: buffer adopted");
  int drawn_q = render(&mq, fb_q);

  CHECK_MSG(drawn_f > 0, "float mesh draws");
  CHECK_MSG(drawn_q == drawn_f, "quantised mesh draws the same triangles");
  int lit = 0, diff = 0;
  for (int i = 0; i < W * H; i++) {
    if (fb_f[i]) lit++;
    if (fb_f[i] != fb_q[i]) diff++;
  }
  CHECK_MSG(lit > 500, "the shape covers pixels");
  CHECK_MSG(diff * 100 <= lit, "at most 1% of lit pixels differ");
}
