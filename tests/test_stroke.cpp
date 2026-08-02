// stroke(): turning an outline into a ribbon.
//
// The case that motivated this file: three or more exactly-collinear points
// produced runaway geometry - a 48-point run spanning 7px stroked to a bounding
// box 20283px wide. Two causes, both about a corner that is not really a corner.
//
// A straight run leaves the two offset edges at a vertex very nearly one line,
// where a crossing point thousands of pixels away is as easy to compute as the
// right answer a hair over |offset| out. The outer side of a join has always
// rejected those, via the miter limit; the concave side kept whatever it was
// given. And the crossing itself was solved in absolute coordinates, so a shape
// a few hundred pixels from the origin carried three leading digits into both
// terms of a numerator where they cancelled - which is why the same run drew
// cleanly at one screen position and exploded at another.
//
// So the assertions here are about *bounds*, not exact points: a stroked path
// may not escape the box its outline plus its own join limit allows, wherever it
// sits and whichever way it points.

#include <cmath>

#include "test.hpp"
#include "picovector.hpp"
#include "shape.hpp"

using namespace picovector;

namespace {

  bool near_f(float a, float b, float eps = 0.001f) { return fabsf(a - b) < eps; }

  struct bounds_t {
    float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
    bool finite = true;

    void add(const vec2_t &v) {
      if(!(v.x == v.x) || !(v.y == v.y)) { finite = false; return; }
      if(v.x < minx) minx = v.x;
      if(v.x > maxx) maxx = v.x;
      if(v.y < miny) miny = v.y;
      if(v.y > maxy) maxy = v.y;
    }
    float span() const { return fmaxf(maxx - minx, maxy - miny); }
  };

  // A straight run of `n` points `len` long from (x, y) at `deg`, stroked.
  bounds_t stroked_run(int n, float x, float y, float deg, float len,
                       float thickness, uint32_t flags, float miter_limit = 4.0f) {
    float a = deg * (float)PV_PI / 180.0f;
    float step = len / (float)(n - 1);
    path_t p;
    for(int i = 0; i < n; i++)
      p.add_point(x + cosf(a) * step * (float)i, y + sinf(a) * step * (float)i);
    p.stroke(thickness, flags, miter_limit);

    bounds_t b;
    for(const vec2_t &v : p.points) b.add(v);
    return b;
  }

}

void test_stroke() {
  // ALIGN_OUTER (the default) puts the band `thickness` outside the outline, so
  // a join may legitimately reach miter_limit * thickness past a vertex.
  const uint32_t OPEN_MITER = PATH_OPEN | JOIN_MITER | CAP_BUTT;
  const float LEN = 7.0f, THICK = 1.0f, LIMIT = 4.0f;
  const float ALLOWED = LEN + 2.0f * LIMIT * THICK;   // 15px

  printf("stroke: collinear points stay inside the outline plus the join limit\n");
  {
    // The reported case, at the position that used to break it.
    bounds_t b = stroked_run(48, 137.3f, 88.7f, 45.0f, LEN, THICK, OPEN_MITER, LIMIT);
    CHECK(b.finite);
    CHECK(b.span() <= ALLOWED);

    // Three points is the smallest run that has an interior vertex at all, and
    // it produced the worst blowup of the lot.
    b = stroked_run(3, 306.6f, 216.6f, 77.0f, LEN, THICK, OPEN_MITER, LIMIT);
    CHECK(b.finite);
    CHECK(b.span() <= ALLOWED);
  }

  printf("stroke: ...wherever on screen it lands, and whichever way it points\n");
  {
    // The failure depended on position and angle, because those decide whether
    // the float maths keeps the collinearity exact. One run is not a test.
    int checked = 0, escaped = 0, infinite = 0;
    for(int n : {3, 8, 48}) {
      for(float x = 0.0f; x < 320.0f; x += 13.0f) {
        for(float y = 0.0f; y < 240.0f; y += 11.0f) {
          for(float deg = 0.0f; deg < 180.0f; deg += 11.0f) {
            bounds_t b = stroked_run(n, x, y, deg, LEN, THICK, OPEN_MITER, LIMIT);
            checked++;
            if(!b.finite) infinite++;
            else if(b.span() > ALLOWED) escaped++;
          }
        }
      }
    }
    CHECK(checked > 10000);
    CHECK_MSG(infinite == 0, "a stroked point was NaN or infinite");
    CHECK_MSG(escaped == 0, "a stroked run escaped its allowed bounds");
  }

  printf("stroke: a run of repeated points is degenerate, not explosive\n");
  {
    // Zero-length edges: the normal has no direction to take, so this is the
    // other way a vertex stops being a corner.
    path_t p;
    for(int i = 0; i < 8; i++) p.add_point(50.0f, 60.0f);
    p.stroke(THICK, OPEN_MITER, LIMIT);
    bounds_t b;
    for(const vec2_t &v : p.points) b.add(v);
    CHECK(b.finite);
    CHECK(b.span() <= ALLOWED);
  }

  printf("stroke: subdividing an edge does not change the stroke\n");
  {
    // The invariant the reported bug broke. A right angle drawn with three
    // points, and the same right angle with each leg subdivided into eight
    // collinear ones: the extra vertices are not corners and must not behave
    // like them, so both have to stroke to the same box.
    path_t bare;
    bare.add_point(100.0f, 100.0f);
    bare.add_point(108.0f, 100.0f);
    bare.add_point(108.0f, 108.0f);

    path_t split;
    for(int i = 0; i <= 8; i++) split.add_point(100.0f + (float)i, 100.0f);
    for(int i = 1; i <= 8; i++) split.add_point(108.0f, 100.0f + (float)i);

    const uint32_t f = PATH_OPEN | JOIN_MITER | CAP_BUTT;
    bare.stroke(THICK, f, LIMIT);
    split.stroke(THICK, f, LIMIT);

    bounds_t a, b;
    for(const vec2_t &v : bare.points) a.add(v);
    for(const vec2_t &v : split.points) b.add(v);
    CHECK(a.finite && b.finite);
    CHECK(fabsf(a.minx - b.minx) < 0.001f && fabsf(a.maxx - b.maxx) < 0.001f);
    CHECK(fabsf(a.miny - b.miny) < 0.001f && fabsf(a.maxy - b.maxy) < 0.001f);
    // ...and it is the 8x8 outline plus the band, not something larger.
    CHECK(near_f(a.maxx - a.minx, 8.0f) && near_f(a.maxy - a.miny, 8.0f));
  }

  printf("stroke: a real corner still gets its miter\n");
  {
    // Bounding the concave join must not flatten geometry that means it. A right
    // angle mitres to a single ring point at the offset lines' crossing; a bevel
    // would put two there instead.
    path_t p;
    p.add_point(100.0f, 100.0f);
    p.add_point(108.0f, 100.0f);
    p.add_point(108.0f, 108.0f);

    auto ring = p.offset_ring(1.0f, false, JOIN_MITER, 4.0f);
    CHECK(ring.size() == 3);                       // one point per vertex
    CHECK(near_f(ring[1].x, 107.0f) && near_f(ring[1].y, 101.0f));
  }

  printf("stroke: every alignment, join and cap is bounded\n");
  {
    for(uint32_t align : {(uint32_t)ALIGN_OUTER, (uint32_t)ALIGN_INNER, (uint32_t)ALIGN_CENTER}) {
      for(uint32_t join : {(uint32_t)JOIN_MITER, (uint32_t)JOIN_ROUND, (uint32_t)JOIN_BEVEL}) {
        for(uint32_t cap : {(uint32_t)CAP_BUTT, (uint32_t)CAP_ROUND, (uint32_t)CAP_SQUARE}) {
          for(uint32_t closed : {(uint32_t)PATH_OPEN, (uint32_t)PATH_CLOSED}) {
            bounds_t b = stroked_run(24, 137.3f, 88.7f, 45.0f, LEN, THICK,
                                     align | join | cap | closed, LIMIT);
            CHECK(b.finite);
            CHECK(b.span() <= ALLOWED);
          }
        }
      }
    }
  }

  printf("stroke: two collinear segments have no intersection to find\n");
  {
    // The helper underneath all of the above. Exactly parallel is the easy case;
    // the one that mattered is two edges that *should* be collinear and differ
    // by an ulp after their normals are normalised.
    vec2_t m;
    CHECK(!intersection(vec2_t(0, 0), vec2_t(10, 0), vec2_t(10, 0), vec2_t(20, 0), m));
    CHECK(!intersection(vec2_t(0, 0), vec2_t(10, 10), vec2_t(10, 10), vec2_t(20, 20), m));
    CHECK(!intersection(vec2_t(0, 0), vec2_t(10, 0), vec2_t(0, 5), vec2_t(10, 5), m));
    // a zero-length edge has no direction, so it meets nothing
    CHECK(!intersection(vec2_t(3, 3), vec2_t(3, 3), vec2_t(0, 0), vec2_t(10, 10), m));

    // A real crossing still resolves, and lands in the same place whether it is
    // solved near the origin or far from it - that is the whole point of doing
    // it in a local frame.
    CHECK(intersection(vec2_t(0, 0), vec2_t(10, 10), vec2_t(0, 10), vec2_t(10, 0), m));
    CHECK(fabsf(m.x - 5.0f) < 0.001f && fabsf(m.y - 5.0f) < 0.001f);

    const float FAR = 8000.0f;
    vec2_t n;
    CHECK(intersection(vec2_t(FAR, FAR), vec2_t(FAR + 10, FAR + 10),
                       vec2_t(FAR, FAR + 10), vec2_t(FAR + 10, FAR), n));
    CHECK(fabsf((n.x - FAR) - 5.0f) < 0.01f && fabsf((n.y - FAR) - 5.0f) < 0.01f);
  }
}
