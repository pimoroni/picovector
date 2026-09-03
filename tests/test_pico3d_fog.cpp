// Linear depth fog in the mesh draw stage.
//
// The point of it being depth fog rather than a point light at the eye: it must
// depend only on how far away a surface is, never on which way it faces. A
// light's falloff is scaled by n.L, so a wall seen square-on comes out brighter
// than one seen edge-on at the same distance - which reads as a torch, not
// atmosphere. These assertions are mostly about that independence.

#include <cmath>

#include "test.hpp"
#include "pico3d.hpp"

using namespace picovector;

namespace {

  const int W = 64, H = 64;
  uint32_t colour[W * H];
  uint16_t depth[W * H];
  pico3d_vcache_t cache[8];

  // One quad facing the camera at distance `dist`, optionally yawed so its
  // normal turns away while its distance stays the same.
  uint32_t draw_quad(float dist, float yaw_degrees, uint32_t fog,
                     float fog_near, float fog_far) {
    const float pos[] = { -1, -1, 0,  1, -1, 0,  1, 1, 0,  -1, 1, 0 };
    const float nrm[] = { 0,0,1, 0,0,1, 0,0,1, 0,0,1 };
    const uint16_t idx[] = { 0, 1, 2, 0, 2, 3 };
    pico3d_mesh_t mesh{};
    mesh.positions = pos; mesh.normals = nrm; mesh.indices = idx;
    mesh.vertex_count = 4; mesh.triangle_count = 2;

    // Red surface, blue fog: then the blue channel carries the fog blend and
    // nothing else, and the red carries the light. One probe each.
    pico3d_material_t m{};
    m.color = pico3d_rgb(255, 0, 0);
    m.double_sided = true;

    pico3d_light_t light{};
    light.direction = vec3_t(0, 0, -1);
    light.color = pico3d_rgb(255, 255, 255);
    light.ambient = pico3d_rgb(0, 0, 0);

    mat4_t model; model.rotate_y(yaw_degrees);
    mat4_t view; view.look_at(vec3_t(0, 0, dist), vec3_t(0, 0, 0), vec3_t(0, 1, 0));
    mat4_t vp; vp.perspective(60.0f, 1.0f, 0.1f, 80.0f); vp.multiply(view);

    for (int i = 0; i < W * H; i++) { colour[i] = 0; depth[i] = 0xFFFF; }
    pico3d_target_t t{};
    t.color = colour; t.depth = depth;
    t.width = W; t.height = H; t.color_stride = W; t.depth_stride = W;
    t.clip_x1 = W; t.clip_y1 = H;
    t.fog = fog; t.fog_near = fog_near; t.fog_far = fog_far;

    pico3d_draw_mesh(&t, &mesh, &model, &vp, &m, PICO3D_GOURAUD, &light, cache);
    return colour[(H / 2) * W + (W / 2)];   // the centre pixel
  }

  int red(uint32_t c) { return (int)(c & 0xff); }
  int blue(uint32_t c) { return (int)((c >> 16) & 0xff); }

}

void test_pico3d_fog() {
  const uint32_t BLUE_FOG = pico3d_rgb(0, 0, 255);

  printf("fog: off by default, so a zeroed target renders unfogged\n");
  {
    uint32_t near_c = draw_quad(3.0f, 0.0f, 0, 0.0f, 0.0f);
    uint32_t far_c = draw_quad(20.0f, 0.0f, 0, 0.0f, 0.0f);
    CHECK(red(near_c) > 200 && blue(near_c) == 0);
    CHECK_MSG(red(far_c) == red(near_c), "distance alone must not change it");
  }

  printf("fog: nearer than fog_near is untouched, past fog_far is the fog colour\n");
  {
    uint32_t inside = draw_quad(3.0f, 0.0f, BLUE_FOG, 5.0f, 15.0f);
    CHECK_MSG(red(inside) > 200 && blue(inside) < 8, "nearer than fog_near");
    uint32_t beyond = draw_quad(40.0f, 0.0f, BLUE_FOG, 5.0f, 15.0f);
    CHECK_MSG(blue(beyond) > 230, "fully fogged should be the fog colour");
    CHECK(red(beyond) < 30);
  }

  printf("fog: the ramp between them is monotonic in distance\n");
  {
    int last = -1;
    for (float d = 5.0f; d <= 15.0f; d += 2.0f) {
      int b = blue(draw_quad(d, 0.0f, BLUE_FOG, 5.0f, 15.0f));
      CHECK(b >= last);
      last = b;
    }
    CHECK(last > 150);
  }

  printf("fog: depends on distance only, not on which way the surface faces\n");
  {
    // Yawing the quad turns its normal away, so the diffuse term drops a long
    // way. The fog blend must not move with it - that is the whole difference
    // between this and a point light parked on the camera.
    uint32_t square_on = draw_quad(10.0f, 0.0f, BLUE_FOG, 5.0f, 15.0f);
    uint32_t angled = draw_quad(10.0f, 55.0f, BLUE_FOG, 5.0f, 15.0f);
    CHECK_MSG(red(angled) < red(square_on), "the light should dim as it turns");
    // The fog is resolved per vertex and interpolated across the triangle, so a
    // yawed quad's two edges sit at slightly different depths and the centre
    // pixel lands a little off the true one. A few counts, not a light's worth.
    CHECK_MSG(abs(blue(angled) - blue(square_on)) <= 8,
              "the fog should not care that it turned");
  }
}
