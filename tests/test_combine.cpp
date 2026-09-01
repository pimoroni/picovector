// Compound shapes: shape_t::append() copies another shape's contours in, so a
// group of primitives fills as one under NON_ZERO instead of as overlapping
// separate draws.

#include <cmath>

#include "test.hpp"
#include "helpers.hpp"

using namespace picovector;
using namespace pvtest;

static shape_t *empty_shape() {
  return new shape_t(0);
}

static shape_t *ring(float cx, float cy, float outer, float inner) {
  shape_t *s = circle(cx, cy, outer);
  shape_t *hole = circle(cx, cy, inner);
  hole->paths[0].reverse();
  s->add_path(hole->paths[0]);
  return s;
}

void test_combine() {
  printf("combine: signed_area reports a winding direction\n");
  {
    shape_t *r = rectangle(10, 10, 20, 10);
    float area = r->paths[0].signed_area();
    CHECK(std::fabs(std::fabs(area) - 2 * 20 * 10) < 0.01f);

    r->paths[0].reverse();
    CHECK(std::fabs(r->paths[0].signed_area() + area) < 0.01f);

    path_t two;
    two.add_point(0, 0);
    two.add_point(1, 1);
    CHECK(two.signed_area() == 0.0f);
    CHECK(path_t().signed_area() == 0.0f);
  }

  printf("combine: reverse() keeps the ring, only the direction\n");
  {
    shape_t *s = star(20, 20, 5, 10, 5);
    std::vector<vec2_t> original(s->paths[0].points.begin(), s->paths[0].points.end());
    s->paths[0].reverse();
    size_t n = original.size();
    bool same = s->paths[0].points.size() == n;
    for(size_t i = 0; same && i < n; i++) {
      same = s->paths[0].points[i].x == original[n - 1 - i].x &&
             s->paths[0].points[i].y == original[n - 1 - i].y;
    }
    CHECK(same);
  }

  printf("combine: the primitives really do disagree on winding\n");
  {
    // what append() exists to paper over: a naive concatenation of these two
    // cancels their overlap to zero winding
    CHECK((circle(0, 0, 10)->paths[0].signed_area() < 0) !=
          (rectangle(-5, -5, 10, 10)->paths[0].signed_area() < 0));
    CHECK((circle(0, 0, 10)->paths[0].signed_area() < 0) ==
          (star(0, 0, 5, 10, 5)->paths[0].signed_area() < 0));
    CHECK((rectangle(-5, -5, 10, 10)->paths[0].signed_area() < 0) ==
          (pie(0, 0, 0, 270, 10)->paths[0].signed_area() < 0));
    // pie() alone flips with its sweep direction
    CHECK((pie(0, 0, 0, 270, 10)->paths[0].signed_area() < 0) !=
          (pie(0, 0, 270, 0, 10)->paths[0].signed_area() < 0));
  }

  printf("combine: append() normalises winding\n");
  {
    shape_t *s = circle(26, 32, 14);
    s->append(*rectangle(26, 24, 24, 16));
    CHECK(s->paths.size() == 2);
    CHECK((s->paths[0].signed_area() < 0) == (s->paths[1].signed_area() < 0));
  }

  printf("combine: the overlap fills solid under NON_ZERO\n");
  {
    shape_t *s = circle(26, 32, 14);
    s->append(*rectangle(26, 24, 24, 16));

    ink_canvas_t c;
    c.img.fill_rule(NON_ZERO);
    c.draw(s);

    // (30, 32) is inside both the circle and the rectangle
    CHECK((c.at(30, 32) & 0xff) == 255);
    CHECK((c.at(20, 32) & 0xff) == 255);
    CHECK((c.at(46, 32) & 0xff) == 255);

    // union area: pi r^2 + w h - overlap, bracketed loosely
    ink_t k = measure_ink(c);
    float circle_area = (float)M_PI * 14 * 14;
    CHECK(k.inked > circle_area && k.inked < circle_area + 24 * 16);
  }

  printf("combine: a naive add_path would have punched a hole\n");
  {
    shape_t *s = circle(26, 32, 14);
    s->add_path(rectangle(26, 24, 24, 16)->paths[0]);

    ink_canvas_t c;
    c.img.fill_rule(NON_ZERO);
    c.draw(s);
    CHECK((c.at(30, 32) & 0xff) == 0);
  }

  printf("combine: a counter-wound hole survives\n");
  {
    shape_t *s = ring(24, 32, 16, 8);
    s->append(*rectangle(40, 28, 18, 8));

    CHECK(s->paths.size() == 3);
    CHECK((s->paths[0].signed_area() < 0) != (s->paths[1].signed_area() < 0));
    CHECK((s->paths[0].signed_area() < 0) == (s->paths[2].signed_area() < 0));

    ink_canvas_t c;
    c.img.fill_rule(NON_ZERO);
    c.draw(s);
    CHECK((c.at(24, 32) & 0xff) == 0);     // still hollow
    CHECK((c.at(24, 18) & 0xff) == 255);   // the ring itself
    CHECK((c.at(50, 32) & 0xff) == 255);   // the appended bar
  }

  printf("combine: the source transform is baked in\n");
  {
    shape_t *moved = rectangle(0, 0, 10, 10);
    moved->transform = mat3_t().translate(30, 30);

    shape_t *s = rectangle(0, 0, 10, 10);
    s->append(*moved);

    rect_t b = s->local_bounds();
    CHECK(std::fabs(b.x) < 0.01f && std::fabs(b.y) < 0.01f);
    CHECK(std::fabs(b.w - 40) < 0.01f && std::fabs(b.h - 40) < 0.01f);

    // the source is left alone
    CHECK(std::fabs(moved->local_bounds().x) < 0.01f);
  }

  printf("combine: a mirroring transform is normalised too\n");
  {
    shape_t *flipped = rectangle(0, 0, 10, 10);
    flipped->transform = mat3_t().scale(-1, 1);

    shape_t *s = rectangle(0, 0, 10, 10);
    s->append(*flipped);
    CHECK((s->paths[0].signed_area() < 0) == (s->paths[1].signed_area() < 0));
  }

  printf("combine: empty shapes either side\n");
  {
    shape_t *s = circle(32, 32, 10);
    size_t before = s->paths.size();
    s->append(*empty_shape());
    CHECK(s->paths.size() == before);

    shape_t *e = empty_shape();
    e->append(*circle(32, 32, 10));
    CHECK(e->paths.size() == 1);
    CHECK(e->paths[0].signed_area() != 0.0f);

    // a degenerate reference gives nothing to match against, so the incoming
    // winding is left as it is
    shape_t *d = new shape_t(1);
    path_t line;
    line.add_point(0, 0);
    line.add_point(10, 0);
    d->add_path(line);
    d->append(*rectangle(0, 0, 10, 10));
    CHECK(d->paths.size() == 2);
  }

  printf("combine: append invalidates the bounds cache\n");
  {
    shape_t *s = rectangle(0, 0, 10, 10);
    CHECK(std::fabs(s->local_bounds().w - 10) < 0.01f);
    s->append(*rectangle(0, 0, 30, 10));
    CHECK(std::fabs(s->local_bounds().w - 30) < 0.01f);
  }
}
