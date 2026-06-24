// Host unit test for the pico3d math layer. Build & run on the host (no Pico SDK):
//   c++ -std=c++17 -I.. test_math.cpp -o test_math && ./test_math
//
// Exits non-zero on failure so it can gate CI.

#include <cstdio>
#include <cmath>

#include "vec3.hpp"
#include "mat4.hpp"

using namespace picovector;

static int failures = 0;

static bool close(float a, float b, float eps = 1e-4f) { return fabsf(a - b) <= eps; }

static void check_vec(const char* name, vec3_t got, vec3_t want, float eps = 1e-4f) {
  bool ok = close(got.x, want.x, eps) && close(got.y, want.y, eps) && close(got.z, want.z, eps);
  if (!ok) {
    failures++;
    printf("FAIL %-28s got (% .4f % .4f % .4f) want (% .4f % .4f % .4f)\n",
           name, got.x, got.y, got.z, want.x, want.y, want.z);
  } else {
    printf("ok   %s\n", name);
  }
}

static void check_f(const char* name, float got, float want, float eps = 1e-4f) {
  if (!close(got, want, eps)) { failures++; printf("FAIL %-28s got % .4f want % .4f\n", name, got, want); }
  else printf("ok   %s\n", name);
}

int main() {
  // --- vec3 ---------------------------------------------------------------
  check_f("dot",      vec3_t(1,2,3).dot(vec3_t(4,5,6)), 32.0f);
  check_vec("cross",  vec3_t(1,0,0).cross(vec3_t(0,1,0)), vec3_t(0,0,1));
  check_f("length",   vec3_t(3,4,0).length(), 5.0f);
  check_vec("normalize", vec3_t(0,3,0).normalized(), vec3_t(0,1,0));
  check_vec("lerp",   vec3_t(0,0,0).lerp(vec3_t(10,20,30), 0.5f), vec3_t(5,10,15));

  // --- mat4 transforms ----------------------------------------------------
  check_vec("identity", (mat4_t() * vec3_t(1,2,3)).xyz(), vec3_t(1,2,3));

  check_vec("translate",
            (mat4_t().translate(10, 20, 30) * vec3_t(1,2,3)).xyz(), vec3_t(11,22,33));

  // +x rotated +90deg about Y goes to -z (right-handed)
  check_vec("rotate_y 90", (mat4_t().rotate_y(90) * vec3_t(1,0,0)).xyz(), vec3_t(0,0,-1));
  // +y rotated +90deg about X goes to +z
  check_vec("rotate_x 90", (mat4_t().rotate_x(90) * vec3_t(0,1,0)).xyz(), vec3_t(0,0,1));
  // +x rotated +90deg about Z goes to +y
  check_vec("rotate_z 90", (mat4_t().rotate_z(90) * vec3_t(1,0,0)).xyz(), vec3_t(0,1,0));

  // Chaining: T then R == translate after rotate. Build T*R, apply to origin:
  // R*origin = origin, then +T  => the translation.
  check_vec("chain T*R origin",
            (mat4_t().translate(5,6,7).rotate_y(33) * vec3_t(0,0,0)).xyz(), vec3_t(5,6,7));
  // Order matters: (T*R) vs (R*T) on a non-origin point should differ.
  {
    vec3_t p(1,0,0);
    vec3_t tr = (mat4_t().translate(10,0,0).rotate_y(90) * p).xyz(); // rotate then translate
    vec3_t rt = (mat4_t().rotate_y(90).translate(10,0,0) * p).xyz(); // translate then rotate
    check_vec("T*R p", tr, vec3_t(10,0,-1));
    check_vec("R*T p", rt, vec3_t(0,0,-11));
  }

  // --- perspective: near plane -> NDC z = -1, far -> +1 -------------------
  {
    float near = 0.5f, far = 100.0f;
    mat4_t proj; proj.perspective(60.0f, 320.0f/240.0f, near, far);
    vec4_t cn = proj * vec3_t(0, 0, -near);
    vec4_t cf = proj * vec3_t(0, 0, -far);
    check_f("persp w(near)", cn.w, near);
    check_f("persp ndc z(near)", cn.project().z, -1.0f);
    check_f("persp w(far)",  cf.w, far);
    check_f("persp ndc z(far)",  cf.project().z, 1.0f);
  }

  // --- look_at: eye looking down -z, target in front maps to -z view space-
  {
    mat4_t view; view.look_at(vec3_t(0,0,5), vec3_t(0,0,0), vec3_t(0,1,0));
    // world origin is 5 units in front of the eye => view space z = -5
    check_vec("look_at origin", (view * vec3_t(0,0,0)).xyz(), vec3_t(0,0,-5));
    // a point at the eye maps to the view-space origin
    check_vec("look_at eye", (view * vec3_t(0,0,5)).xyz(), vec3_t(0,0,0));
  }

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
