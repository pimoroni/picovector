// grow() / shrink(): offsetting a shape's outline along its edge normals.
//
// The offset itself is offset_ring, which stroke() shares and test_stroke.cpp
// covers. What is specific here is what grow() adds on top: the winding fix and
// the coincident-point cleanup.
//
// Winding, because offset_ring grows for one winding and shrinks for the other -
// so without the signed-area correction a clockwise and a counter-clockwise
// version of the same square offset in opposite directions for the same call.
// A caller building geometry from mixed sources cannot be expected to know or
// care which way a path happens to be wound.
//
// Coincident points, because a repeated vertex is a zero-length edge with no
// direction to take a normal from, and every closed ring from GeoJSON carries
// one (its first point repeated as its last).

#include <cmath>

#include "test.hpp"
#include "picovector.hpp"
#include "shape.hpp"
#include "primitive.hpp"

using namespace picovector;

namespace {

  bool near_f(float a, float b, float eps = 0.01f) { return fabsf(a - b) < eps; }

  bool bounds_are(shape_t *s, float x, float y, float w, float h) {
    rect_t b = s->bounds();
    return near_f(b.x, x) && near_f(b.y, y) && near_f(b.w, w) && near_f(b.h, h);
  }

  shape_t *square(bool clockwise) {
    shape_t *s = new shape_t();
    path_t p(4);
    if (clockwise) p.points = {{0, 100}, {100, 100}, {100, 0}, {0, 0}};
    else           p.points = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    s->paths.push_back(p);
    return s;
  }

}

void test_grow() {
  printf("grow: a positive amount grows outward whichever way the path is wound\n");
  for (int clockwise = 0; clockwise < 2; clockwise++) {
    shape_t *s = square(clockwise != 0);
    CHECK(bounds_are(s, 0, 0, 100, 100));
    s->grow(10);
    CHECK(bounds_are(s, -10, -10, 120, 120));
    s->shrink(10);
    CHECK(bounds_are(s, 0, 0, 100, 100));
  }

  printf("grow: shrink insets by the same amount it grows\n");
  {
    shape_t *s = square(false);
    s->shrink(10);
    CHECK(bounds_are(s, 10, 10, 80, 80));
  }

  printf("grow: a ring repeating its first point as its last still offsets\n");
  {
    // The repeat is a zero-length edge, and dividing by its length put a NaN in
    // the ring - one NaN point takes the whole shape's bounds with it.
    shape_t *s = new shape_t();
    path_t p(5);
    p.points = {{0, 0}, {100, 0}, {100, 100}, {0, 100}, {0, 0}};
    s->paths.push_back(p);
    s->grow(10);
    CHECK(bounds_are(s, -10, -10, 120, 120));
  }

  printf("grow: a straight run of points does not spike\n");
  {
    // Three collinear points make both offset edges very nearly one line, where
    // the crossing can land thousands of pixels out. Bounds, not points: the
    // offset outline may not escape the box the original plus the offset allows.
    shape_t *s = new shape_t();
    path_t p(4);
    p.points = {{0, 0}, {50, 0}, {100, 0}, {50, 80}};
    s->paths.push_back(p);
    s->grow(5);
    rect_t b = s->bounds();
    CHECK(std::isfinite(b.x) && std::isfinite(b.y));
    CHECK(std::isfinite(b.w) && std::isfinite(b.h));
    CHECK(b.w < 100 + 2 * 5 * 4 && b.h < 80 + 2 * 5 * 4);
  }

  printf("grow: the offset tracks a curve's radius\n");
  {
    // Measured on the points rather than the bounds: a polygonal circle's box
    // sits a segment's sag inside the true radius, and by an uneven amount on
    // each axis, so the box would say the two axes grew by different amounts.
    // Every vertex, though, moves exactly `amount` further out.
    shape_t *s = circle(0, 0, 50);
    auto max_radius = [](shape_t *sh) {
      float r = 0;
      for (const vec2_t &p : sh->paths[0].points) {
        float d = sqrtf(p.x * p.x + p.y * p.y);
        if (d > r) r = d;
      }
      return r;
    };
    float before = max_radius(s);
    s->grow(10);
    CHECK(near_f(max_radius(s) - before, 10.0f, 0.05f));
  }

  printf("grow: a zero amount and a degenerate path are no-ops\n");
  {
    shape_t *s = square(false);
    s->grow(0);
    CHECK(bounds_are(s, 0, 0, 100, 100));

    // Fewer than three distinct points cannot form a ring to offset.
    shape_t *line = new shape_t();
    path_t p(2);
    p.points = {{0, 0}, {100, 0}};
    line->paths.push_back(p);
    line->grow(10);
    CHECK(line->paths[0].points.size() == 2);
  }
}
