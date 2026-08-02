// The gradient LUT.
//
// build_lut is file-static, so these reach it the way an app does: construct a
// gradient brush and read the 256-entry table it built.

#include <cstring>

#include "test.hpp"
#include "helpers.hpp"
#include "picovector.hpp"
#include "color.hpp"
#include "blend.hpp"
#include "brush.hpp"

using namespace picovector;

// One LUT, copied out so only one 1KB brush is live at a time.
static void build(pixel_t *out, const float *pos, const color_t *cols, int n) {
  gradient_brush_t g(GRADIENT_LINEAR, 0, 0, 1, 0, pos, cols, n, nullptr);
  memcpy(out, g.lut, sizeof(pixel_t) * 256);
}

static bool all_equal(const pixel_t *lut, int from, int to, pixel_t want) {
  for(int i = from; i <= to; i++) if(lut[i] != want) return false;
  return true;
}

// how far a colour is from grey, as a stand-in for chroma
static int chroma_of(pixel_t p) {
  int r = (int)_r(p), g = (int)_g(p), b = (int)_b(p);
  int hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
  int lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
  return hi - lo;
}

void test_gradient() {
  pixel_t lut[256];

  const color_t red   = rgb_color_t(255, 0, 0, 255);
  const color_t green = rgb_color_t(0, 255, 0, 255);
  const color_t blue  = rgb_color_t(0, 0, 255, 255);

  printf("gradient: no stops gives a zeroed table\n");
  {
    const float pos[1] = { 0.0f };
    const color_t cols[1] = { red };
    build(lut, pos, cols, 0);
    CHECK(all_equal(lut, 0, 255, 0));
  }

  printf("gradient: one stop fills the whole table\n");
  {
    const float pos[1] = { 0.5f };
    const color_t cols[1] = { red };
    build(lut, pos, cols, 1);
    CHECK(all_equal(lut, 0, 255, red._p));
  }

  printf("gradient: a two-stop ramp lands on its endpoints\n");
  {
    const float pos[2] = { 0.0f, 1.0f };
    const color_t cols[2] = { red, blue };
    build(lut, pos, cols, 2);
    CHECK(lut[0] == red._p);
    CHECK(lut[255] == blue._p);
    for(int i = 0; i < 256; i++) CHECK_MSG(_a(lut[i]) == 255, "alpha drifted mid-ramp");
  }

  printf("gradient: every stop lands exactly on its own entry\n");
  {
    // Offsets become table indices before anything is interpolated, so a stop is
    // reproduced bit-exactly rather than approached by a float ramp.
    const float pos[3] = { 0.0f, 0.25f, 1.0f };
    const color_t cols[3] = { red, green, blue };
    build(lut, pos, cols, 3);
    CHECK(lut[0] == red._p);
    CHECK(lut[64] == green._p);      // 0.25 * 255 rounds to 64
    CHECK(lut[255] == blue._p);
  }

  printf("gradient: each channel moves monotonically across a ramp\n");
  {
    const float pos[2] = { 0.0f, 1.0f };
    const color_t cols[2] = { red, blue };
    build(lut, pos, cols, 2);
    for(int i = 1; i < 256; i++) {
      CHECK(_r(lut[i]) <= _r(lut[i - 1]));   // red falls away
      CHECK(_b(lut[i]) >= _b(lut[i - 1]));   // blue comes up
    }
  }

  printf("gradient: the ends pad\n");
  {
    // 0.25 and 0.75 round to entries 64 and 191, so the ramp occupies 64..191 and
    // everything outside it holds the nearer stop.
    const float pos[2] = { 0.25f, 0.75f };
    const color_t cols[2] = { red, blue };
    build(lut, pos, cols, 2);
    CHECK(all_equal(lut, 0, 64, red._p));      // pad, plus the stop's own entry
    CHECK(all_equal(lut, 191, 255, blue._p));
    CHECK(lut[65] != red._p);
    CHECK(lut[190] != blue._p);
  }

  printf("gradient: offsets are clamped and forced non-decreasing\n");
  {
    // -1 clamps to 0; 0.4 is below the preceding 0.6 so it is pulled up to it,
    // making the middle segment a hard stop; 2.0 clamps to 1.
    const float pos[4] = { -1.0f, 0.6f, 0.4f, 2.0f };
    const color_t cols[4] = { red, green, blue, blue };
    build(lut, pos, cols, 4);
    CHECK(lut[0] == red._p);
    CHECK(lut[255] == blue._p);
    CHECK(_g(lut[152]) > _g(lut[100]));   // still climbing toward green
    CHECK(lut[153] == blue._p);           // and the hard stop lands there
  }

  printf("gradient: a hard stop switches colour in one entry\n");
  {
    // Two stops sharing an offset. The later of the pair takes the entry they
    // share, which is the SVG rule for a discontinuity.
    const float pos[4] = { 0.0f, 0.2f, 0.2f, 1.0f };
    const color_t cols[4] = { red, green, blue, blue };
    build(lut, pos, cols, 4);
    CHECK(lut[0] == red._p);
    CHECK(lut[255] == blue._p);
    CHECK(lut[51] == blue._p);                    // the seam entry
    CHECK(_g(lut[50]) > 240 && _r(lut[50]) > 0);  // still ramping into green
  }

  printf("gradient: a ramp toward a transparent colour keeps its colour\n");
  {
    // The menu's icon highlight: white at alpha 64 toward white at alpha 0.
    // Interpolating the authored components keeps it white the whole way, where
    // recovering them from the premultiplied word could not - alpha-0 white and
    // transparent black are the same word, so the ramp used to head for black and
    // came out at half this brightness mid-ramp.
    const float pos[2] = { 0.0f, 1.0f };
    const color_t cols[2] = {
      rgb_color_t(255, 255, 255, 64),
      rgb_color_t(255, 255, 255, 0),
    };
    build(lut, pos, cols, 2);
    CHECK(_a(lut[0]) == 64 && _r(lut[0]) == 64);
    CHECK(_a(lut[255]) == 0);
    CHECK(_a(lut[128]) == 32);
    CHECK_MSG(_r(lut[128]) == 32, "the highlight is no longer straight-alpha white");
    // premultiplied white means every channel equals the alpha, all the way down
    for(int i = 0; i < 256; i++) {
      CHECK(_r(lut[i]) == _a(lut[i]) && _g(lut[i]) == _a(lut[i]) && _b(lut[i]) == _a(lut[i]));
    }
  }

  printf("gradient: alpha interpolates across a translucent ramp\n");
  {
    const float pos[2] = { 0.0f, 1.0f };
    const color_t cols[2] = {
      rgb_color_t(255, 0, 0, 255),
      rgb_color_t(255, 0, 0, 0),
    };
    build(lut, pos, cols, 2);
    CHECK(_a(lut[0]) == 255);
    CHECK(_a(lut[255]) == 0);
    for(int i = 1; i < 256; i++) CHECK(_a(lut[i]) <= _a(lut[i - 1]));
  }

  printf("gradient: two OKLCH stops ramp through OKLCH\n");
  {
    // Same lightness and chroma, hues a third of a turn apart. Through OKLCH the
    // chroma is held all the way across; through sRGB the midpoint slumps toward
    // grey, which is the whole reason for doing this.
    const color_t oa = oklch_color_t(150, 110, 170, 255);
    const color_t ob = oklch_color_t(150, 110, 60, 255);
    const float pos[2] = { 0.0f, 1.0f };

    const color_t ok[2] = { oa, ob };
    build(lut, pos, ok, 2);
    int ok_mid = chroma_of(lut[128]);
    CHECK(lut[0] == oa._p);
    CHECK(lut[255] == ob._p);

    // the same two endpoint colours, but authored as RGB so the ramp is sRGB
    const color_t as_rgb[2] = {
      rgb_color_t(oa.r(), oa.g(), oa.b(), 255),
      rgb_color_t(ob.r(), ob.g(), ob.b(), 255),
    };
    pixel_t srgb_lut[256];
    build(srgb_lut, pos, as_rgb, 2);
    int srgb_mid = chroma_of(srgb_lut[128]);

    CHECK(lut[0] == srgb_lut[0]);         // same endpoints either way
    CHECK(lut[255] == srgb_lut[255]);
    CHECK_MSG(ok_mid > srgb_mid + 8, "the OKLCH midpoint is no more chromatic than the sRGB one");
  }

  printf("gradient: an OKLCH hue takes the short way round\n");
  {
    // 250 to 10 is 16 counts forward through zero, not 240 backwards. Going the
    // long way would pass through the opposite side of the wheel, so the midpoint
    // is the tell.
    const color_t a = oklch_color_t(150, 110, 250, 255);
    const color_t b = oklch_color_t(150, 110, 10, 255);
    const float pos[2] = { 0.0f, 1.0f };
    const color_t cols[2] = { a, b };
    build(lut, pos, cols, 2);

    // the short way keeps every entry close to one of the two ends
    const color_t expect = a.mix(b, 128);
    CHECK(lut[128] == expect._p);
    CHECK(expect.h() == 2);
  }

  printf("gradient: a segment between spaces falls back to sRGB, joins stay exact\n");
  {
    const color_t a = oklch_color_t(150, 110, 170, 255);
    const color_t b = rgb_color_t(20, 40, 200, 255);
    const color_t c = oklch_color_t(200, 60, 60, 255);
    const float pos[3] = { 0.0f, 0.5f, 1.0f };
    const color_t cols[3] = { a, b, c };
    build(lut, pos, cols, 3);

    // whatever each segment interpolates through, the stops themselves are exact
    CHECK(lut[0] == a._p);
    CHECK(lut[128] == b._p);   // 0.5 * 255 rounds to 128
    CHECK(lut[255] == c._p);
  }

  printf("gradient: the full 16 stops are honoured\n");
  {
    float pos[gradient_brush_t::max_stops];
    color_t cols[gradient_brush_t::max_stops];
    for(int i = 0; i < gradient_brush_t::max_stops; i++) {
      pos[i] = (float)i / (float)(gradient_brush_t::max_stops - 1);
      cols[i] = rgb_color_t((uint8_t)(i * 17), 0, (uint8_t)(255 - i * 17), 255);
    }
    build(lut, pos, cols, gradient_brush_t::max_stops);
    CHECK(lut[0] == cols[0]._p);
    CHECK(lut[255] == cols[gradient_brush_t::max_stops - 1]._p);
    for(int i = 1; i < 256; i++) CHECK(_r(lut[i]) >= _r(lut[i - 1]));
  }

  printf("gradient: building the same stops twice gives the same table\n");
  {
    const float pos[3] = { 0.0f, 0.3f, 1.0f };
    const color_t cols[3] = { red, green, blue };
    pixel_t again[256];
    build(lut, pos, cols, 3);
    build(again, pos, cols, 3);
    CHECK(memcmp(lut, again, sizeof(lut)) == 0);
  }

  printf("gradient: geometry() moves the brush and leaves the table alone\n");
  {
    const float pos[2] = { 0.0f, 1.0f };
    const color_t cols[2] = { red, blue };
    gradient_brush_t g(GRADIENT_LINEAR, 0, 0, 1, 0, pos, cols, 2, nullptr);

    pixel_t before[256];
    memcpy(before, g.lut, sizeof(before));

    g.geometry(3, 4, 5, 6, nullptr);
    CHECK(g.p1.x == 3.0f && g.p1.y == 4.0f);
    CHECK(g.p2.x == 5.0f && g.p2.y == 6.0f);
    CHECK(memcmp(before, g.lut, sizeof(before)) == 0);

    // a transform is inverted on the way in, and dropping it restores identity
    mat3_t m;
    m = m.translate(10, 20);
    g.geometry(0, 0, 1, 0, &m);
    CHECK(g.base_inverse.v02 == -10.0f && g.base_inverse.v12 == -20.0f);
    g.geometry(0, 0, 1, 0, nullptr);
    CHECK(g.base_inverse.v02 == 0.0f && g.base_inverse.v12 == 0.0f);
    CHECK(memcmp(before, g.lut, sizeof(before)) == 0);
  }

  printf("gradient: a conical sweep runs clockwise from its start direction\n");
  {
    // A 64x64 canvas with the sweep centred, starting straight up. Sampling the
    // four compass points should walk a quarter of the ramp at a time,
    // clockwise: up is the start, then right, down, left.
    const float pos[2] = { 0.0f, 1.0f };
    const color_t cols[2] = { rgb_color_t(0, 0, 0, 255), rgb_color_t(255, 255, 255, 255) };

    pvtest::canvas_t c(64, 64);
    c.flat(0xff000000u);
    gradient_brush_t g(GRADIENT_CONICAL, 32, 32, 32, 0, pos, cols, 2, nullptr);
    c.img.brush(&g);
    c.img.rectangle(rect_t(0, 0, 64, 64));

    int up    = (int)_r(c.at(32, 6));
    int right = (int)_r(c.at(58, 32));
    int down  = (int)_r(c.at(32, 58));
    int left  = (int)_r(c.at(6, 32));
    CHECK_MSG(up < 8, "the sweep does not start at its start direction");
    CHECK_MSG(right > 55 && right < 72, "a quarter turn clockwise is not a quarter of the ramp");
    CHECK_MSG(down > 120 && down < 136, "a half turn is not half of the ramp");
    CHECK_MSG(left > 185 && left < 201, "three quarters of a turn is not three quarters of the ramp");
    CHECK(up < right && right < down && down < left);
  }

  printf("gradient: a conical sweep starts where p2 points\n");
  {
    const float pos[2] = { 0.0f, 1.0f };
    const color_t cols[2] = { rgb_color_t(0, 0, 0, 255), rgb_color_t(255, 255, 255, 255) };

    // start to the right instead: the ramp's zero moves a quarter turn with it
    pvtest::canvas_t c(64, 64);
    c.flat(0xff000000u);
    gradient_brush_t g(GRADIENT_CONICAL, 32, 32, 63, 32, pos, cols, 2, nullptr);
    c.img.brush(&g);
    c.img.rectangle(rect_t(0, 0, 64, 64));

    CHECK((int)_r(c.at(58, 32)) < 8);          // right is now zero
    CHECK((int)_r(c.at(32, 6)) > 185);         // up is three quarters round
  }

  printf("gradient: an unknown gradient type falls back to linear\n");
  {
    const float pos[2] = { 0.0f, 1.0f };
    const color_t cols[2] = { red, blue };
    gradient_brush_t g((gradient_type_t)7, 0, 0, 1, 0, pos, cols, 2, nullptr);
    CHECK(g.type == GRADIENT_LINEAR);
  }

  printf("gradient: only a gradient answers as_gradient()\n");
  {
    const float pos[2] = { 0.0f, 1.0f };
    const color_t cols[2] = { red, blue };
    gradient_brush_t g(GRADIENT_LINEAR, 0, 0, 1, 0, pos, cols, 2, nullptr);
    color_brush_t cb(rgb_color_t(255, 0, 0, 255));
    brush_t *bg = &g, *bc = &cb;
    CHECK(bg->as_gradient() == &g);
    CHECK(bc->as_gradient() == nullptr);
  }

  printf("gradient: the brush stays inside its expected footprint\n");
  {
    // 1KB of it is the LUT.
    CHECK(sizeof(gradient_brush_t) <= 1152);
    CHECK(gradient_brush_t::max_stops == 16);
  }
}
