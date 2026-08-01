// vec2 / rect / mat3: the value types the rest of the API is expressed in.

#include <cmath>

#include "test.hpp"
#include "picovector.hpp"
#include "types.hpp"
#include "mat3.hpp"
#include "shape.hpp"
#include "primitive.hpp"

using namespace picovector;

static bool near(float a, float b, float eps = 0.001f) { return std::fabs(a - b) < eps; }

void test_geometry() {
  printf("geometry: vec2\n");
  {
    vec2_t a(3, 4);
    CHECK(near(a.length(), 5.0f));
    vec2_t n = a.normalized();
    CHECK(near(n.length(), 1.0f));
    CHECK(near((a + vec2_t(1, 1)).x, 4.0f));
    CHECK(near((a - vec2_t(1, 1)).y, 3.0f));
    CHECK(near(a.dot(vec2_t(1, 0)), 3.0f));
    vec2_t zero(0, 0);
    CHECK(!std::isnan(zero.normalized().x));   // must not divide by zero
  }

  printf("geometry: rect\n");
  {
    rect_t r(10, 20, 30, 40);
    CHECK(near(r.tl().x, 10.0f) && near(r.tl().y, 20.0f));
    CHECK(!r.empty());
    CHECK(rect_t(0, 0, 0, 0).empty());

    rect_t i = r.intersection(rect_t(20, 30, 100, 100));
    CHECK(near(i.x, 20.0f) && near(i.y, 30.0f) && near(i.w, 20.0f) && near(i.h, 30.0f));
    CHECK(r.intersection(rect_t(100, 100, 5, 5)).empty());
    CHECK(r.intersects(rect_t(20, 30, 5, 5)));
    CHECK(!r.intersects(rect_t(100, 100, 5, 5)));
  }

  printf("geometry: mat3\n");
  {
    mat3_t t;
    t = t.translate(10, 20);
    vec2_t p = vec2_t(1, 2).transform(&t);
    CHECK(near(p.x, 11.0f) && near(p.y, 22.0f));

    mat3_t s;
    s = s.scale(2.0f, 3.0f);
    vec2_t q = vec2_t(4, 5).transform(&s);
    CHECK(near(q.x, 8.0f) && near(q.y, 15.0f));

    mat3_t r;
    r = r.rotate(90.0f);
    vec2_t u = vec2_t(1, 0).transform(&r);
    CHECK(near(std::fabs(u.y), 1.0f, 0.01f));
    CHECK(near(u.x, 0.0f, 0.01f));

    // trs() must match building the same transform step by step
    mat3_t chained;
    chained = chained.translate(5, 6);
    chained = chained.rotate(30.0f);
    chained = chained.scale(2.0f, 2.0f);
    mat3_t once = mat3_t::trs(5, 6, 30.0f, 2.0f, 2.0f);
    vec2_t v(3, 7);
    vec2_t a = v.transform(&chained), b = v.transform(&once);
    CHECK(near(a.x, b.x, 0.01f) && near(a.y, b.y, 0.01f));

    // inverse round-trips
    mat3_t m = mat3_t::trs(11, -4, 25.0f, 1.5f, 1.5f);
    mat3_t inv = m;
    inv.inverse();
    vec2_t there = v.transform(&m);
    vec2_t back = there.transform(&inv);
    CHECK(near(back.x, v.x, 0.01f) && near(back.y, v.y, 0.01f));
  }

  printf("geometry: shape bounds follow the geometry\n");
  {
    shape_t *c = circle(50, 60, 10);
    rect_t b = c->bounds();
    CHECK(near(b.x, 40.0f, 0.5f) && near(b.y, 50.0f, 0.5f));
    CHECK(near(b.w, 20.0f, 0.5f) && near(b.h, 20.0f, 0.5f));

    shape_t *r = rectangle(5, 6, 20, 10);
    rect_t rb = r->bounds();
    CHECK(near(rb.x, 5.0f) && near(rb.y, 6.0f) && near(rb.w, 20.0f) && near(rb.h, 10.0f));

    // an empty sweep must not produce a degenerate or inverted box
    shape_t *a = arc(50, 50, 0, 0, 10, 20);
    CHECK(a->bounds().w >= 0.0f && a->bounds().h >= 0.0f);
  }

  printf("geometry: curve tessellation scales with radius\n");
  {
    shape_t *small = circle(0, 0, 5);
    shape_t *large = circle(0, 0, 150);
    size_t ns = small->paths[0].points.size(), nl = large->paths[0].points.size();
    CHECK(ns >= 30);              // floor
    CHECK(nl <= 120);             // cap
    CHECK(nl > ns);               // and it does scale between them
  }
}
