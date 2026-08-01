// Rasterising: every primitive across a range of sizes, the two fill rules,
// antialiasing, and the tile seams a shape larger than one tile crosses.
//
// Properties rather than golden images, so a legitimate change to edge coverage
// doesn't invalidate the suite.

#include <cmath>

#include "test.hpp"
#include "helpers.hpp"

using namespace picovector;
using namespace pvtest;

struct named_shape_t { const char *name; shape_t *s; };

static std::vector<named_shape_t> shapes_at(float size) {
  const float c = 32.0f;
  return {
    {"circle",           circle(c, c, size)},
    {"ellipse",          ellipse(c, c, size, size * 0.6f)},
    {"rectangle",        rectangle(c - size, c - size, size * 2, size * 2)},
    {"rounded_rectangle",rounded_rectangle(c - size, c - size, size * 2, size * 2,
                                           size * 0.3f, size * 0.3f, size * 0.3f, size * 0.3f)},
    {"squircle",         squircle(c, c, size, 4.0f)},
    {"arc",              arc(c, c, -135, 135, size * 0.5f, size)},
    {"pie",              pie(c, c, 0, 270, size)},
    {"star",             star(c, c, 5, size, size * 0.5f)},
    {"line",             line(c - size, c - size, c + size, c + size, 3)},
    {"regular_polygon",  regular_polygon(c, c, 6, size)},
  };
}

void test_raster() {
  printf("raster: every primitive draws, inside its own bounds\n");
  for(float size : {2.0f, 5.0f, 13.0f, 28.0f}) {
    for(auto &ns : shapes_at(size)) {
      ink_canvas_t c;
      c.draw(ns.s);
      ink_t k = measure_ink(c);
      CHECK_MSG(k.inked > 0, ns.name);
      if(k.inked == 0) continue;
      rect_t b = ns.s->bounds();
      bool inside = k.minx >= (int)b.x - 1 && k.miny >= (int)b.y - 1 &&
                    k.maxx <= (int)(b.x + b.w) + 1 && k.maxy <= (int)(b.y + b.h) + 1;
      CHECK_MSG(inside, ns.name);
    }
  }

  printf("raster: a circle's area brackets pi r^2\n");
  for(int r : {6, 12, 24}) {
    ink_canvas_t c;
    c.draw(circle(32, 32, (float)r));
    ink_t k = measure_ink(c);
    float area = 3.14159265f * r * r;
    CHECK(k.full <= area && area <= k.inked);
  }

  printf("raster: a ring has a hole\n");
  for(auto ir : {std::pair<float,float>{10, 25}, {20, 24}}) {
    ink_canvas_t c;
    c.draw(arc(32, 32, 0, 360, ir.first, ir.second));
    CHECK((c.at(32, 32) & 0xff) == 0);
    CHECK((c.at(32, 32 - (int)((ir.first + ir.second) / 2)) & 0xff) > 0);
  }

  printf("raster: antialiasing only softens an edge\n");
  {
    ink_canvas_t hard(64, 64, OFF);
    hard.draw(circle(32, 32, 24));
    ink_canvas_t soft(64, 64, X4);
    soft.draw(circle(32, 32, 24));
    ink_t h = measure_ink(hard), s = measure_ink(soft);
    CHECK(h.partial == 0);
    CHECK(s.partial > 0);
    CHECK(s.inked >= h.inked);
    CHECK(s.full <= h.inked);
    CHECK((hard.at(32, 32) & 0xff) == 255);
    CHECK((soft.at(32, 32) & 0xff) == 255);
  }

  printf("raster: X2 and X4 agree (the analytic path has nothing to supersample)\n");
  {
    ink_canvas_t a(64, 64, X2), b(64, 64, X4);
    a.draw(circle(32, 32, 21));
    b.draw(circle(32, 32, 21));
    CHECK(a.pixels == b.pixels);
  }

  printf("raster: fill rules\n");
  {
    // A pentagram genuinely crosses itself. shape.star() does not - it alternates
    // an outer and an inner radius, tracing a decagon whose outline never
    // crosses, so both rules agree on it.
    path_t p(5);
    for(int i = 0; i < 5; i++) {
      float a = (-90 + i * 144) * 3.14159265f / 180.0f;
      p.add_point(32 + cosf(a) * 28, 32 + sinf(a) * 28);
    }
    shape_t *penta = new(PV_MALLOC(sizeof(shape_t))) shape_t(1);
    penta->add_path(p);

    ink_canvas_t eo(64, 64, OFF), nz(64, 64, OFF);
    eo.img.fill_rule(EVEN_ODD);  eo.draw(penta);
    nz.img.fill_rule(NON_ZERO);  nz.draw(penta);
    ink_t e = measure_ink(eo), n = measure_ink(nz);
    CHECK(e.inked != n.inked);
    CHECK(n.inked >= e.inked);
    CHECK((eo.at(32, 32) & 0xff) == 0);      // hollow under even-odd
    CHECK((nz.at(32, 32) & 0xff) > 0);       // filled under non-zero

    ink_canvas_t eo2(64, 64, X4), nz2(64, 64, X4);
    eo2.img.fill_rule(EVEN_ODD); eo2.draw(circle(32, 32, 20));
    nz2.img.fill_rule(NON_ZERO); nz2.draw(circle(32, 32, 20));
    CHECK(eo2.pixels == nz2.pixels);         // convex: the rules must agree
  }

  printf("raster: offscreen and partially offscreen\n");
  for(auto p : {std::pair<float,float>{-40, 32}, {104, 32}, {32, -40}, {32, 104}}) {
    ink_canvas_t c;
    c.draw(circle(p.first, p.second, 20));
    CHECK(measure_ink(c).inked == 0);
  }
  for(auto p : {std::pair<float,float>{0, 32}, {64, 32}, {32, 0}, {32, 64}}) {
    ink_canvas_t c;
    c.draw(circle(p.first, p.second, 20));
    ink_t k = measure_ink(c);
    CHECK(k.inked > 0);
    CHECK(k.minx >= 0 && k.miny >= 0 && k.maxx < 64 && k.maxy < 64);
  }

  printf("raster: tile seams\n");
  {
    // Tiles are 160x120, so anything larger is rasterised in several passes and
    // must come out identical wherever it sits relative to the seams.
    ink_canvas_t seam(320, 240, X4), inside(320, 240, X4);
    seam.draw(circle(160, 120, 50));      // centred on both seams
    inside.draw(circle(80, 60, 50));      // wholly inside one tile
    CHECK(std::abs(measure_ink(seam).inked - measure_ink(inside).inked) <= 2);

    bool row_same = true, col_same = true;
    for(int i = 0; i <= 100; i++) {
      if((seam.at(110 + i, 120) & 0xff) != (inside.at(30 + i, 60) & 0xff)) row_same = false;
      if((seam.at(160, 70 + i) & 0xff) != (inside.at(80, 10 + i) & 0xff)) col_same = false;
    }
    CHECK(row_same);
    CHECK(col_same);

    ink_canvas_t big(320, 240, X4);
    big.draw(arc(160, 120, 0, 360, 80, 110));
    CHECK((big.at(160, 120) & 0xff) == 0);          // hole survives four tiles
    CHECK((big.at(160, 120 - 95) & 0xff) > 0);
  }

  printf("raster: drawing twice\n");
  {
    ink_canvas_t once(64, 64, OFF), twice(64, 64, OFF);
    once.draw(circle(32, 32, 20));
    twice.draw(circle(32, 32, 20));
    twice.draw(circle(32, 32, 20));
    CHECK(once.pixels == twice.pixels);             // hard edges are idempotent
  }
}
