// Inputs the API has to survive rather than trust.
//
// The classes here are the ones this code is structurally prone to: it carves
// fixed buffers out of one shared pool, does its own bounds arithmetic, and
// parses a container format straight off a filesystem. None of that is checked
// by the drawing tests, which only ever pass sensible values.

#include <cmath>
#include <vector>

#include "test.hpp"
#include "helpers.hpp"
#include "font.hpp"
#include "pixel_font.hpp"

using namespace picovector;
using namespace pvtest;

// An image wrapped in a canary arena, so anything written outside its buffer is
// visible rather than merely corrupting whatever is next in memory.
struct arena_t {
  static const int PAD = 4096;
  std::vector<uint32_t> mem;
  image_t img;
  int w, h;

  arena_t(int w, int h)
    : mem((size_t)PAD * 2 + (size_t)w * h, 0xDEADBEEFu),
      img(mem.data() + PAD, w, h), w(w), h(h) {}

  bool intact() const {
    for(int i = 0; i < PAD; i++) if(mem[i] != 0xDEADBEEFu) return false;
    for(size_t i = PAD + (size_t)w * h; i < mem.size(); i++)
      if(mem[i] != 0xDEADBEEFu) return false;
    return true;
  }
};

static void draw(image_t *img, shape_t *s) {
  rgb_color_t c(255, 0, 0, 255);
  color_brush_t b(c);
  mat3_t t;
  render(s, img, &t, &b);
}

static shape_t *path_of(const std::vector<vec2_t> &pts) {
  shape_t *s = new(PV_MALLOC(sizeof(shape_t))) shape_t(1);
  path_t p((int)pts.size());
  for(auto &v : pts) p.add_point(v);
  s->add_path(p);
  return s;
}

// A deterministic PRNG, so a failure here is reproducible rather than a story
// about one unlucky CI run.
static uint32_t rnd_state = 12345;
static uint32_t rnd() {
  rnd_state ^= rnd_state << 13;
  rnd_state ^= rnd_state >> 17;
  rnd_state ^= rnd_state << 5;
  return rnd_state;
}

void test_robustness() {
  printf("robust: degenerate shapes draw nothing and touch nothing\n");
  {
    arena_t a(64, 64);
    draw(&a.img, circle(32, 32, 0));
    draw(&a.img, circle(32, 32, -20));
    draw(&a.img, rectangle(10, 10, -30, -30));
    draw(&a.img, arc(32, 32, 0, 360, 10, 10));      // zero-width band
    draw(&a.img, arc(32, 32, 90, 90, 10, 20));      // zero sweep
    draw(&a.img, pie(32, 32, 45, 45, 20));
    draw(&a.img, path_of({}));                      // empty path
    draw(&a.img, path_of({vec2_t(10, 10)}));        // one point
    draw(&a.img, path_of({vec2_t(10, 10), vec2_t(10, 10)}));  // repeated point
    CHECK(a.intact());
  }

  printf("robust: coordinates far outside the image\n");
  {
    // Spans carry int16 coordinates, so a shape that starts inside and runs a
    // long way out must be clipped before it reaches them, not after.
    arena_t a(64, 64);
    draw(&a.img, rectangle(-40000, 20, 80000, 10));
    draw(&a.img, circle(32, 32, 40000));
    draw(&a.img, line(-50000, -50000, 50000, 50000, 4));
    draw(&a.img, rectangle(32760, 32760, 100, 100));
    CHECK(a.intact());
  }

  printf("robust: NaN and infinity in coordinates\n");
  {
    arena_t a(64, 64);
    float nan = std::nanf(""), inf = INFINITY;
    draw(&a.img, rectangle(nan, nan, 10, 10));
    draw(&a.img, rectangle(10, 10, nan, nan));
    draw(&a.img, circle(inf, 32, 10));
    draw(&a.img, circle(32, 32, inf));
    draw(&a.img, circle(32, 32, nan));
    draw(&a.img, path_of({vec2_t(nan, 0), vec2_t(inf, 10), vec2_t(20, 20)}));
    CHECK(a.intact());
  }

  printf("robust: more geometry than the fixed buffers hold\n");
  {
    // MAX_EDGES is 1024 and a contour over the glyph buffer is dropped; either
    // way the answer is to draw less, never to write more.
    arena_t a(64, 64);
    std::vector<vec2_t> many;
    for(int k = 0; k < 4000; k++) {
      float ang = k * 6.2831853f / 4000;
      many.push_back(vec2_t(32 + cosf(ang) * 30, 32 + sinf(ang) * 30));
    }
    draw(&a.img, path_of(many));

    // a shape whose rows each cross the outline many times
    std::vector<vec2_t> comb;
    for(int k = 0; k < 200; k++) {
      float x = 2 + k * 0.3f;
      comb.push_back(vec2_t(x, 4));
      comb.push_back(vec2_t(x, 60));
    }
    draw(&a.img, path_of(comb));
    CHECK(a.intact());
  }

  printf("robust: more spans than the span buffer holds\n");
  {
    // The span buffer is a fixed 8KB global, so an overrun lands in whatever
    // follows it rather than in the image - the canary arena cannot see it and
    // the sanitiser job is what catches it. What is checkable here is that the
    // drawing still comes out right once an emitter starts splitting batches.
    //
    // The rasteriser is the tight one: a tile is TILE_HEIGHT (120) rows and each
    // row can emit MAX_NODES_PER_SCANLINE/2 (16) spans, which is 1920 against a
    // 1365-span capacity. A comb across a full-height tile reaches it.
    const int W = 320, H = 240;
    arena_t a(W, H);
    std::vector<vec2_t> comb;
    for(int k = 0; k < 200; k++) {
      float x = 2 + k * 1.5f;
      comb.push_back(vec2_t(x, 4));
      comb.push_back(vec2_t(x, H - 4));
    }
    draw(&a.img, path_of(comb));
    CHECK(a.intact());

    // Splitting a batch must not lose spans. A solid fill taller than the
    // buffer is one span a row, so every row is checkable.
    rgb_color_t red(255, 0, 0, 255);
    {
      canvas_t c(8, PV_SOLID_SPAN_CAP + 200);
      c.flat(0xff000000u);
      color_brush_t b(red);
      c.img.brush(&b);
      c.img.rectangle(rect_t(0, 0, 8, c.h));
      int filled = 0;
      for(int y = 0; y < c.h; y++) if(c.at(4, y) == red._p) filled++;
      CHECK_MSG(filled == c.h, "a fill taller than the span buffer lost rows");
    }

    // The same for vspan(), a column of one-pixel spans.
    {
      canvas_t c(8, PV_SOLID_SPAN_CAP + 200);
      c.flat(0xff000000u);
      color_brush_t b(red);
      c.img.brush(&b);
      c.img.vspan(3, 0, c.h);
      int filled = 0;
      for(int y = 0; y < c.h; y++) if(c.at(3, y) == red._p) filled++;
      CHECK_MSG(filled == c.h, "a column taller than the span buffer lost rows");
    }

    // line() emits one span per pixel, so a long diagonal outruns the buffer.
    {
      const int N = PV_SOLID_SPAN_CAP + 200;
      canvas_t c(N, N);
      c.flat(0xff000000u);
      color_brush_t b(red);
      c.img.brush(&b);
      c.img.line(vec2_t(0, 0), vec2_t(N - 1, N - 1));
      int lit = 0;
      for(int i = 0; i < N; i++) if(c.at(i, i) == red._p) lit++;
      CHECK_MSG(lit == N, "a line longer than the span buffer lost pixels");
    }

    // draw_glyph() multiplies runs by rows by scale. A checkerboard glyph is
    // four runs a row: 4 x 8 rows x 60 = 1920 spans against a 1365 capacity.
    {
      uint8_t bitmap[8] = { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 };
      pixel_font_glyph_t g = { 'X', 8 };
      pixel_font_t f{};
      f.width = 8;
      f.height = 8;
      f.glyph_count = 1;
      f.glyph_data_size = 8;
      f.glyphs = &g;
      f.glyph_data = bitmap;

      const int scale = 60;
      canvas_t c(8 * scale, 8 * scale);
      c.flat(0xff000000u);
      color_brush_t b(red);
      c.img.brush(&b);
      c.snapshot();
      f.draw_glyph(&c.img, &g, bitmap, &b, c.img.clip(), 0, 0, scale);
      // the checkerboard is half lit, and no batch split may drop any of it
      CHECK_MSG(c.changed_total() == (8 * scale) * (8 * scale) / 2,
                "a scaled glyph lost spans to a batch split");
    }
  }

  printf("robust: a shape too big for the edge buffer draws nothing, not part of itself\n");
  {
    // MAX_EDGES is 1024 and a contour that doesn't fit is refused. What makes
    // that dangerous is the fill rule: inside is decided from the edges in the
    // batch, so a ring missing its inner contour is not a partial ring, it is a
    // solid disc where the hole should be. Splitting across two flushes has the
    // same problem, each flush seeing only its own edges - so it is the whole
    // shape or none of it.
    auto ring = [](int outer_pts, int inner_pts) {
      shape_t *s = new(PV_MALLOC(sizeof(shape_t))) shape_t(2);
      for(auto spec : {std::pair<int,float>{outer_pts, 100.0f},
                       std::pair<int,float>{inner_pts, 50.0f}}) {
        path_t p(spec.first);
        for(int i = 0; i < spec.first; i++) {
          float a = i * 6.2831853f / spec.first;
          p.add_point(vec2_t(160 + cosf(a) * spec.second, 120 + sinf(a) * spec.second));
        }
        s->add_path(p);
      }
      return s;
    };
    auto lit_and_hole = [](shape_t *s, int &lit, bool &hole_open) {
      canvas_t c(320, 240);
      c.flat(0xff000000u);
      rgb_color_t red(255, 0, 0, 255);
      color_brush_t b(red);
      c.img.brush(&b);
      mat3_t t;
      render(s, &c.img, &t, &b);
      lit = c.changed_total();
      hole_open = c.at(160, 120) != red._p;
    };

    int lit = 0; bool hole_open = false;

    // comfortably inside the buffer: a ring, with its hole
    lit_and_hole(ring(200, 100), lit, hole_open);
    CHECK(lit > 0);
    CHECK_MSG(hole_open, "a ring that fits lost its hole");
    int good_lit = lit;

    // right up to the limit, still a ring
    lit_and_hole(ring(900, 100), lit, hole_open);
    CHECK_MSG(hole_open, "a ring at the edge of the buffer lost its hole");
    CHECK(lit > 0);

    // over it: nothing at all, rather than a disc with the hole filled in
    lit_and_hole(ring(1000, 100), lit, hole_open);
    CHECK_MSG(lit == 0, "a shape too big for the edge buffer drew part of itself");

    // and a single contour over the limit is refused the same way
    lit_and_hole(ring(2000, 0), lit, hole_open);
    CHECK(lit == 0);

    (void)good_lit;
  }

  printf("robust: a glyph contour over the point buffer drops the glyph, not a piece of it\n");
  {
    // Same rule for text: the glyph scratch buffer holds 512 points and the
    // edge buffer 1024, and a glyph over either is a shape that cannot be
    // rasterised correctly. An 'o' missing its outer contour is a filled blob.
    auto be16 = [](std::vector<uint8_t> &v, int n) {
      v.push_back((uint8_t)((n >> 8) & 0xff));
      v.push_back((uint8_t)(n & 0xff));
    };
    // One glyph whose contours have the given point counts, each a circle at a
    // different radius so the geometry is real. Wide points, 16-bit contour
    // counts. Mixed counts matter: a glyph where only *some* contours are over
    // a limit is the case that tells a dropped piece from a dropped glyph.
    auto build_af = [&](std::vector<int> contour_pts) {
      int total = 0;
      for(int p : contour_pts) total += p;
      std::vector<uint8_t> v = { 'a','f','!','?' };
      be16(v, 3);                       // flags: 16-bit point counts, wide
      be16(v, 1);                       // glyph_count
      be16(v, (int)contour_pts.size()); // path_count
      be16(v, total);                   // point_count
      be16(v, 128);                     // units_per_em
      be16(v, 'A');                     // codepoint
      be16(v, 0); be16(v, 0);           // x, y
      be16(v, 100); be16(v, 100);       // w, h
      be16(v, 110);                     // advance
      v.push_back((uint8_t)contour_pts.size());
      for(int p : contour_pts) be16(v, p);
      for(size_t c = 0; c < contour_pts.size(); c++)
        for(int i = 0; i < contour_pts[c]; i++) {
          float a = i * 6.2831853f / contour_pts[c], r = 50.0f - (float)c * 10.0f;
          be16(v, (int)(cosf(a) * r));
          be16(v, (int)(sinf(a) * r));
        }
      return v;
    };
    auto ink = [](const std::vector<uint8_t> &blob) {
      font_t f;
      uint8_t *buf = nullptr;
      size_t n = 0;
      if(parse_vector_font(blob.data(), blob.size(), &f, &buf, &n) != FONT_OK) return -1;
      canvas_t c(256, 256);
      c.flat(0xff000000u);
      rgb_color_t pen(255, 255, 255, 255);
      color_brush_t b(pen);
      c.img.brush(&b);
      c.img.font(&f);
      c.img.text_cursor(vec2_t(128, 128));   // clear of the edges at this size
      c.snapshot();
      f.draw(&c.img, "A", 80.0f);
      int lit = c.changed_total();
      PV_FREE(buf);
      return lit;
    };

    // one contour over the 512-point scratch buffer, one comfortably under, so
    // the survivor would draw on its own if the glyph were not dropped whole
    CHECK_MSG(ink(build_af({300, 300})) > 0, "a glyph within both limits drew nothing");
    CHECK_MSG(ink(build_af({100, 100})) > 0, "the control contours draw on their own");
    CHECK_MSG(ink(build_af({600, 100})) == 0,
              "a glyph with an over-long contour drew part of itself");
    // each contour fits the scratch buffer, the three together exceed MAX_EDGES
    CHECK_MSG(ink(build_af({400, 400, 400})) == 0,
              "a glyph over the edge buffer drew part of itself");
  }

  printf("robust: a primitive's vertex count is bounded before it allocates\n");
  {
    // These counts come straight off the Python API, so they reach reserve()
    // unfiltered. A huge one is an unbounded allocation, a negative one
    // reserves a huge size_t (std::length_error, which on a -fno-exceptions
    // firmware build is an abort), and a NaN one is an undefined cast.
    auto count = [](shape_t *s) {
      size_t n = 0;
      for(auto &p : s->paths) n += p.points.size();
      return n;
    };
    float nan = std::nanf(""), inf = INFINITY;
    arena_t a(64, 64);

    // sane inputs are untouched
    CHECK(count(regular_polygon(32, 32, 6, 20)) == 6);
    CHECK(count(star(32, 32, 5, 20, 10)) == 10);
    CHECK(count(rounded_rectangle(0, 0, 50, 50, 10, 10, 10, 10)) == 16);

    for(auto s : {regular_polygon(32, 32, 1e9f, 20), regular_polygon(32, 32, -5, 20),
                  regular_polygon(32, 32, nan, 20), regular_polygon(32, 32, inf, 20),
                  star(32, 32, 100000, 20, 10),     star(32, 32, -5, 20, 10),
                  rounded_rectangle(0, 0, 50, 50, 1e6f, 0, 0, 0),
                  rounded_rectangle(0, 0, 50, 50, nan, nan, nan, nan),
                  rounded_rectangle(0, 0, 50, 50, inf, 0, 0, 0)}) {
      CHECK_MSG(count(s) >= 3, "a primitive degenerated below a drawable path");
      CHECK_MSG(count(s) <= 4 * PV_CURVE_MAX_SIDES + 8,
                "a primitive's vertex count was not bounded");
      draw(&a.img, s);                 // and it still rasterises without incident
    }
    CHECK(a.intact());
  }

  printf("robust: tiny images\n");
  {
    for(auto wh : {std::pair<int,int>{1, 1}, {1, 64}, {64, 1}, {2, 2}}) {
      arena_t a(wh.first, wh.second);
      rgb_color_t c(255, 0, 0, 255);
      color_brush_t b(c);
      a.img.brush(&b);
      a.img.clear();
      a.img.rectangle(rect_t(0, 0, wh.first, wh.second));
      a.img.blur(4.0f);
      a.img.bloom(100, 150, 4.0f);
      a.img.wave(4, 4);
      a.img.zoom(200);
      a.img.oilpaint(3, 128);
      a.img.chromatic(3);
      draw(&a.img, circle(0, 0, 10));
      CHECK(a.intact());
    }
  }

  printf("robust: drawing into a sub-image window stays in the window\n");
  {
    canvas_t c(64, 64);
    c.flat(0xff000000u);
    image_t win = c.img.window(rect_t(16, 16, 16, 16));
    rgb_color_t red(255, 0, 0, 255);
    color_brush_t b(red);
    win.brush(&b);
    c.snapshot();
    win.clear();
    CHECK(c.changed_total() == 16 * 16);
    CHECK(c.changed_outside(rect_t(16, 16, 16, 16)) == 0);

    c.flat(0xff000000u);                             // back to black, or red on red
    c.snapshot();                                    // would register as no change
    mat3_t t;
    render(circle(8, 8, 20), &win, &t, &b);          // larger than the window
    CHECK(c.changed_total() > 0);
    CHECK(c.changed_outside(rect_t(16, 16, 16, 16)) == 0);
  }

  printf("robust: a singular matrix doesn't poison coordinates with NaN\n");
  {
    for(auto m : {mat3_t().scale(0.0f, 0.0f), mat3_t().scale(1.0f, 0.0f),
                  mat3_t().scale(0.0f, 1.0f)}) {
      mat3_t inv = m;
      inv.inverse();
      vec2_t v = vec2_t(3, 4).transform(&inv);
      CHECK(std::isfinite(v.x) && std::isfinite(v.y));
    }
    // and a well-conditioned one still inverts
    mat3_t m = mat3_t::trs(11, -4, 25.0f, 1.5f, 1.5f);
    mat3_t inv = m;
    inv.inverse();
    vec2_t v(3, 7);
    vec2_t back = v.transform(&m).transform(&inv);
    CHECK(std::fabs(back.x - v.x) < 0.01f && std::fabs(back.y - v.y) < 0.01f);
  }

  printf("robust: blitting an image onto itself\n");
  {
    arena_t a(32, 32);
    for(int i = 0; i < 32 * 32; i++) ((uint32_t *)a.img.ptr(0, 0))[i] = 0xff000000u | (uint32_t)i;
    a.img.blit(&a.img, vec2_t(4, 4));
    a.img.blit(&a.img, rect_t(0, 0, 32, 32), rect_t(8, 8, 16, 16));
    CHECK(a.intact());
  }

  printf("robust: a truncated UTF-8 sequence at the end of a bounded span\n");
  {
    // The length-bounded draw/measure overloads exist so a caller can point into
    // the middle of a buffer with no NUL to stop on, which makes `end` the only
    // thing standing between the decoder and the next allocation. A multi-byte
    // lead byte as the last byte of the span is the case that tests it.
    //
    // Exact-sized heap allocations, so a read past the end is a sanitiser
    // report rather than a byte that happened to be readable.
    const std::vector<uint8_t> good = {
      'a','f','!','?', 0,3, 0,1, 0,1, 0,4, 4,0,
      0,'A', 0,2, 0,0, 0,40, 0,50, 0,44, 1,
      0,4,
      0,0, 0,0, 0,30, 0,0, 0,30, 0,40, 0,0, 0,40 };
    font_t f;
    uint8_t *fbuf = nullptr;
    size_t fn = 0;
    CHECK(parse_vector_font(good.data(), good.size(), &f, &fbuf, &fn) == FONT_OK);

    canvas_t c(32, 32);
    rgb_color_t pen(255, 255, 255, 255);
    color_brush_t b(pen);
    c.img.brush(&b);
    c.img.font(&f);

    pixel_font_glyph_t pg = { 'A', 8 };
    uint8_t pgdata[8] = { 0xFF, 0x81, 0x81, 0xFF, 0x81, 0x81, 0x81, 0x00 };
    pixel_font_t pf{};
    pf.width = 8; pf.height = 8; pf.glyph_count = 1; pf.glyph_data_size = 8;
    pf.glyphs = &pg; pf.glyph_data = pgdata;
    c.img.pixel_font(&pf);

    // 0xC3 leads a 2-byte sequence, 0xE0 a 3-byte one; both are the last byte.
    for(uint8_t lead : {(uint8_t)0xC3, (uint8_t)0xE0}) {
      for(int n : {1, 2}) {
        char *span = new char[n];
        for(int i = 0; i < n - 1; i++) span[i] = 'A';
        span[n - 1] = (char)lead;                 // truncated sequence at the end
        f.measure(&c.img, span, span + n, 10.0f);
        f.draw(&c.img, span, span + n, 10.0f);
        pf.measure(&c.img, span, span + n, 1);
        c.img.text_cursor(vec2_t(0, 0));
        pf.draw(&c.img, span, span + n, 1);
        delete[] span;
      }
    }
    PV_FREE(fbuf);
  }

  printf("robust: the .af parser against 20000 mutated fonts\n");
  {
    // The parser reads a file off a filesystem, so every field in it is
    // attacker- or corruption-controlled. It must reject or accept, never run
    // off the end of the buffer.
    const std::vector<uint8_t> good = {
      'a','f','!','?', 0,3, 0,1, 0,1, 0,4, 4,0,
      0,'A', 0,2, 0,0, 0,40, 0,50, 0,44, 1,
      0,4,
      0,0, 0,0, 0,30, 0,0, 0,30, 0,40, 0,0, 0,40 };

    int accepted = 0, rejected = 0;
    bool sane = true;
    for(int i = 0; i < 20000; i++) {
      std::vector<uint8_t> m = good;
      int muts = 1 + (int)(rnd() % 4);
      for(int k = 0; k < muts && !m.empty(); k++) {
        uint32_t op = rnd() % 3;
        if(op == 0) m[rnd() % m.size()] = (uint8_t)rnd();
        else if(op == 1) m.resize(1 + rnd() % m.size());
        else m.push_back((uint8_t)rnd());
      }
      font_t f;
      uint8_t *buf = nullptr;
      size_t n = 0;
      font_status_t s = parse_vector_font(m.data(), m.size(), &f, &buf, &n);
      if(s == FONT_OK) {
        accepted++;
        // an accepted font must describe itself consistently
        if(f.glyph_count < 0 || f.units_per_em <= 0.0f) sane = false;
        for(int g = 0; g < f.glyph_count && sane; g++)
          for(int pth = 0; pth < f.glyphs[g].path_count && sane; pth++)
            if(f.glyphs[g].paths[pth].points == nullptr) sane = false;
        // The block belongs to the caller on success - font_t only points into
        // it - so a fuzz loop that keeps none of them has to hand every one
        // back. Six thousand of these leaked ~114MB, which only Linux CI sees:
        // ASan's leak detector is unavailable on Darwin.
        PV_FREE(buf);
      } else {
        rejected++;
      }
    }
    CHECK(accepted > 0);        // the mutations aren't all fatal
    CHECK(rejected > 0);        // nor all harmless
    CHECK_MSG(sane, "an accepted font described itself inconsistently");
  }

  printf("robust: a rejected font hands its block back\n");
  {
    // The parser allocates one block up front and only publishes it through
    // `buffer` on FONT_OK, so a rejection after that point is the one path that
    // has to free it itself. A device retrying a corrupt font does this on a
    // loop, and under the default PV_MALLOC (no tracing GC) every attempt is
    // gone for good.
    //
    // What is asserted here is the observable contract: rejected, and `buffer`
    // left alone. The free itself is LSan's, which runs with ASan on the Linux
    // CI leg - it is unsupported on macOS, so a local run proves the contract
    // but not the free.
    const std::vector<uint8_t> good = {
      'a','f','!','?', 0,3, 0,1, 0,1, 0,4, 4,0,
      0,'A', 0,2, 0,0, 0,40, 0,50, 0,44, 1,
      0,4,
      0,0, 0,0, 0,30, 0,0, 0,30, 0,40, 0,0, 0,40 };

    // claimed_paths > path_count: the glyph says two contours, the header one.
    // Byte 26 is the glyph's path_count: 14 of header, then a wide glyph's
    // codepoint/x/y/w/h/advance at two bytes each.
    std::vector<uint8_t> too_many_paths = good;
    too_many_paths[26] = 2;
    // truncated part-way through the point data
    std::vector<uint8_t> truncated(good.begin(), good.end() - 6);

    for(auto &bad : {too_many_paths, truncated}) {
      font_t f;
      uint8_t *buf = (uint8_t *)0x1;              // must stay untouched on failure
      size_t n = 0;
      font_status_t s = parse_vector_font(bad.data(), bad.size(), &f, &buf, &n);
      CHECK_MSG(s != FONT_OK, "a malformed font was accepted");
      CHECK_MSG(buf == (uint8_t *)0x1, "a rejected font published its block");
    }

    // Repeated, because that is how the leak presented: a font that fails to
    // load once fails every time the caller retries it.
    for(int i = 0; i < 64; i++) {
      font_t f;
      uint8_t *buf = nullptr;
      size_t n = 0;
      CHECK(parse_vector_font(too_many_paths.data(), too_many_paths.size(),
                              &f, &buf, &n) != FONT_OK);
      CHECK(buf == nullptr);
    }
  }
}

// Indexed images: the storage is shared with sub-views and sized to the source,
// which is what makes a paletted spritesheet worth having.
void test_palette() {
  printf("palette: an indexed image is one byte a pixel\n");
  {
    image_t img(64, 64, RGBA8888, true, 16);
    CHECK(img.has_palette());
    CHECK(img.palette_size() == 16);
    CHECK(img.buffer_size() == 64 * 64);            // not 64*64*4
  }

  printf("palette: the table is sized to what was asked for\n");
  {
    for(auto n : {2, 4, 16, 256}) {
      image_t img(8, 8, RGBA8888, true, n);
      CHECK(img.palette_size() == n);
    }
    image_t clamped(8, 8, RGBA8888, true, 4000);
    CHECK(clamped.palette_size() == 256);           // a byte index reaches no further
    image_t none(8, 8, RGBA8888, false, 256);
    CHECK(none.palette_size() == 0);                // no table when not indexed
  }

  printf("palette: an out-of-range index is ignored, not written\n");
  {
    image_t img(8, 8, RGBA8888, true, 4);
    img.palette(0, 0xff112233u);
    img.palette(200, 0xffaabbccu);                  // past the end of a 4-entry table
    CHECK(img.palette(0) == 0xff112233u);
    CHECK(img.palette(200) == 0);
  }

  printf("palette: sub-views share the table rather than copying it\n");
  {
    image_t sheet(64, 64, RGBA8888, true, 16);
    sheet.palette(3, 0xff445566u);
    image_t sprite = sheet.window(rect_t(16, 16, 16, 16));
    CHECK(sprite.has_palette());
    CHECK(sprite.palette_size() == 16);
    CHECK(sprite.palette_data() == sheet.palette_data());   // the same storage
    CHECK(sprite.palette(3) == 0xff445566u);
    // and a later change on the parent is visible through the view
    sheet.palette(3, 0xff778899u);
    CHECK(sprite.palette(3) == 0xff778899u);
  }

  printf("palette: writing to an indexed image is refused, not attempted\n");
  {
    // Every brush and filter stores a four-byte pixel. An indexed image is one
    // byte a pixel, so any of them would write four times past the end of every
    // row - image.oilpaint() on a GIF was heap corruption, not a bad picture.
    // A canary arena either side proves nothing lands outside the buffer.
    const uint32_t guard = 0xDEADBEEFu;
    const int w = 32, h = 32, pad = 1024;
    std::vector<uint32_t> arena(pad * 2 + (size_t)w * h, guard);
    uint8_t *pixels = (uint8_t *)(arena.data() + pad);

    image_t sheet(pixels, w, h, RGBA8888, true, 8);
    for(int i = 0; i < w * h; i++) pixels[i] = (uint8_t)(i % 8);
    sheet.palette(3, rgb_color_t(10, 20, 30, 255)._p);

    color_brush_t pen(rgb_color_t(255, 255, 255, 255));
    sheet.brush(&pen);

    sheet.clear();
    sheet.rectangle(rect_t(2, 2, 8, 8));
    sheet.circle(vec2_t(16, 16), 10);
    sheet.line(vec2_t(0, 0), vec2_t(31, 31));
    sheet.put(vec2_t(4, 4));
    sheet.blur(3.0f, 1.0f);
    sheet.bloom(180, 150, 4.0f);
    sheet.wave(4, 4);
    sheet.zoom(128);
    sheet.edgeglow(220);
    sheet.invert();
    sheet.oilpaint(3, 128);

    bool intact = true;
    for(int i = 0; i < pad; i++) if(arena[i] != guard) intact = false;
    for(size_t i = pad + (size_t)w * h; i < arena.size(); i++) if(arena[i] != guard) intact = false;
    CHECK_MSG(intact, "a write to an indexed image ran outside its buffer");

    // And the indices themselves are left exactly as they were.
    bool unchanged = true;
    for(int i = 0; i < w * h; i++) if(pixels[i] != (uint8_t)(i % 8)) unchanged = false;
    CHECK_MSG(unchanged, "an indexed image was written to");

    // Blitting into one is refused the same way; blitting out of one still works.
    canvas_t dst(w, h);
    dst.flat(0xff000000u);
    sheet.blit(&dst.img, vec2_t(0, 0));
    CHECK(dst.at(3, 0) == rgb_color_t(10, 20, 30, 255)._p);

    canvas_t src(8, 8);
    src.flat(0xffffffffu);
    src.img.blit(&sheet, vec2_t(0, 0));
    for(int i = 0; i < w * h; i++) if(pixels[i] != (uint8_t)(i % 8)) unchanged = false;
    CHECK_MSG(unchanged, "an indexed image was blitted into");
  }

  printf("palette: an indexed image blits its colours, not its indices\n");
  {
    image_t src(8, 8, RGBA8888, true, 4);
    src.palette(0, 0xff0000ffu);
    src.palette(1, 0xff00ff00u);
    for(int y = 0; y < 8; y++)
      for(int x = 0; x < 8; x++)
        ((uint8_t *)src.ptr(0, y))[x] = (x < 4) ? 0 : 1;

    canvas_t dst(16, 16);
    dst.flat(0xff000000u);
    src.blit(&dst.img, vec2_t(0, 0));
    CHECK(dst.at(1, 1) == 0xff0000ffu);
    CHECK(dst.at(6, 1) == 0xff00ff00u);

    // and a palette write recolours everything indexing it
    src.palette(0, 0xffff00ffu);
    src.blit(&dst.img, vec2_t(0, 0));
    CHECK(dst.at(1, 1) == 0xffff00ffu);
  }
}
