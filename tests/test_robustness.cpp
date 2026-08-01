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
      } else {
        rejected++;
      }
    }
    CHECK(accepted > 0);        // the mutations aren't all fatal
    CHECK(rejected > 0);        // nor all harmless
    CHECK_MSG(sane, "an accepted font described itself inconsistently");
  }
}
