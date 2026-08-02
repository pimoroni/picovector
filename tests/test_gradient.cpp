// The gradient LUT.
//
// build_lut is file-static, so these reach it the way an app does: construct a
// gradient brush and read the 256-entry table it built. Written before the stop
// colours change from a premultiplied word to a color_t, so it pins the current
// behaviour first - including one case that is wrong today and is expected to
// change (see "a ramp toward a transparent colour").

#include <cstring>

#include "test.hpp"
#include "picovector.hpp"
#include "color.hpp"
#include "blend.hpp"
#include "brush.hpp"

using namespace picovector;

// One LUT, copied out so only one 1KB brush is live at a time.
static void build(pixel_t *out, const float *pos, const pixel_t *cols, int n) {
  gradient_brush_t g(GRADIENT_LINEAR, 0, 0, 1, 0, pos, cols, n, nullptr);
  memcpy(out, g.lut, sizeof(pixel_t) * 256);
}

static bool all_equal(const pixel_t *lut, int from, int to, pixel_t want) {
  for(int i = from; i <= to; i++) if(lut[i] != want) return false;
  return true;
}

void test_gradient() {
  pixel_t lut[256];

  const pixel_t red   = rgb_color_t(255, 0, 0, 255)._p;
  const pixel_t green = rgb_color_t(0, 255, 0, 255)._p;
  const pixel_t blue  = rgb_color_t(0, 0, 255, 255)._p;

  printf("gradient: no stops gives a zeroed table\n");
  {
    const float pos[1] = { 0.0f };
    const pixel_t cols[1] = { red };
    build(lut, pos, cols, 0);
    CHECK(all_equal(lut, 0, 255, 0));
  }

  printf("gradient: one stop fills the whole table\n");
  {
    const float pos[1] = { 0.5f };
    const pixel_t cols[1] = { red };
    build(lut, pos, cols, 1);
    CHECK(all_equal(lut, 0, 255, red));
  }

  printf("gradient: a two-stop ramp lands on its endpoints\n");
  {
    const float pos[2] = { 0.0f, 1.0f };
    const pixel_t cols[2] = { red, blue };
    build(lut, pos, cols, 2);
    CHECK(lut[0] == red);
    CHECK(lut[255] == blue);
    // and every entry is opaque, since both stops are
    for(int i = 0; i < 256; i++) CHECK_MSG(_a(lut[i]) == 255, "alpha drifted mid-ramp");
  }

  printf("gradient: each channel moves monotonically across a ramp\n");
  {
    const float pos[2] = { 0.0f, 1.0f };
    const pixel_t cols[2] = { red, blue };
    build(lut, pos, cols, 2);
    for(int i = 1; i < 256; i++) {
      CHECK(_r(lut[i]) <= _r(lut[i - 1]));   // red falls away
      CHECK(_b(lut[i]) >= _b(lut[i - 1]));   // blue comes up
    }
  }

  printf("gradient: the ends pad, and the pad boundaries sit where t crosses\n");
  {
    // t is i/255, and build_lut pads on `t <= pos[0]` / `t >= pos[n-1]`, so the
    // first stop holds through i = 63 (0.247) and the last from i = 192 (0.753).
    const float pos[2] = { 0.25f, 0.75f };
    const pixel_t cols[2] = { red, blue };
    build(lut, pos, cols, 2);
    CHECK(all_equal(lut, 0, 63, red));
    CHECK(all_equal(lut, 192, 255, blue));
    CHECK(lut[64] != red);
    // Not symmetric at the far end: rounding lands entry 191 on blue exactly,
    // one before the pad begins.
  }

  printf("gradient: offsets are clamped and forced non-decreasing\n");
  {
    // -1 clamps to 0; 0.4 is below the preceding 0.6 so it is pulled up to it,
    // making the middle segment a hard stop; 2.0 clamps to 1.
    const float pos[4] = { -1.0f, 0.6f, 0.4f, 2.0f };
    const pixel_t cols[4] = { red, green, blue, blue };
    build(lut, pos, cols, 4);
    CHECK(lut[0] == red);
    CHECK(lut[255] == blue);
    // green is reached at t = 0.6 (i = 153) and the hard stop is there too
    CHECK(_g(lut[153]) > _g(lut[100]));
  }

  printf("gradient: a hard stop switches colour in one entry\n");
  {
    // Two stops sharing an offset. 0.2 is exactly 51/255, so an entry lands on
    // the seam: the segment before it reaches f == 1 and resolves to the *later*
    // of the two, which is the SVG rule, and the next entry is already past it.
    // The zero-width segment itself is never sampled, because the segment
    // search skips any whose end offset is below t.
    const float pos[4] = { 0.0f, 0.2f, 0.2f, 1.0f };
    const pixel_t cols[4] = { red, green, blue, blue };
    build(lut, pos, cols, 4);
    CHECK(lut[0] == red);
    CHECK(lut[255] == blue);
    CHECK(lut[51] == green);                  // the seam entry itself
    CHECK(lut[52] == blue);                   // and one entry later, fully across
    CHECK(_g(lut[50]) > 240 && _r(lut[50]) > 0);  // still ramping into green
  }

  printf("gradient: a ramp toward a transparent colour loses its colour today\n");
  {
    // The menu's icon highlight (apps/menu/app.py): white at alpha 64 toward
    // white at alpha 0. The alpha-0 stop premultiplies to the word 0, and
    // unpremultiply() cannot tell that from transparent black, so the ramp heads
    // for black instead of for transparent white. Mid-ramp the premultiplied
    // channels come out at half what straight-alpha interpolation would give.
    //
    // EXPECTED TO CHANGE: once stops carry their authored components, lut[128]
    // should hold 32, not 16.
    const float pos[2] = { 0.0f, 1.0f };
    const pixel_t cols[2] = {
      rgb_color_t(255, 255, 255, 64)._p,
      rgb_color_t(255, 255, 255, 0)._p,
    };
    build(lut, pos, cols, 2);
    CHECK(_a(lut[0]) == 64 && _r(lut[0]) == 64);     // opaque end is already right
    CHECK(_a(lut[255]) == 0);
    CHECK(_a(lut[128]) == 32);                        // alpha interpolates correctly
    CHECK_MSG(_r(lut[128]) == 16, "premultiplied mid-ramp changed (32 is correct)");
  }

  printf("gradient: alpha interpolates across a translucent ramp\n");
  {
    const float pos[2] = { 0.0f, 1.0f };
    const pixel_t cols[2] = {
      rgb_color_t(255, 0, 0, 255)._p,
      rgb_color_t(255, 0, 0, 0)._p,
    };
    build(lut, pos, cols, 2);
    CHECK(_a(lut[0]) == 255);
    CHECK(_a(lut[255]) == 0);
    for(int i = 1; i < 256; i++) CHECK(_a(lut[i]) <= _a(lut[i - 1]));
  }

  printf("gradient: the full 16 stops are honoured\n");
  {
    float pos[gradient_brush_t::max_stops];
    pixel_t cols[gradient_brush_t::max_stops];
    for(int i = 0; i < gradient_brush_t::max_stops; i++) {
      pos[i] = (float)i / (float)(gradient_brush_t::max_stops - 1);
      cols[i] = rgb_color_t((uint8_t)(i * 17), 0, (uint8_t)(255 - i * 17), 255)._p;
    }
    build(lut, pos, cols, gradient_brush_t::max_stops);
    CHECK(lut[0] == cols[0]);
    CHECK(lut[255] == cols[gradient_brush_t::max_stops - 1]);
    for(int i = 1; i < 256; i++) CHECK(_r(lut[i]) >= _r(lut[i - 1]));
  }

  printf("gradient: building the same stops twice gives the same table\n");
  {
    const float pos[3] = { 0.0f, 0.3f, 1.0f };
    const pixel_t cols[3] = { red, green, blue };
    pixel_t again[256];
    build(lut, pos, cols, 3);
    build(again, pos, cols, 3);
    CHECK(memcmp(lut, again, sizeof(lut)) == 0);
  }

  printf("gradient: geometry() moves the brush and leaves the table alone\n");
  {
    const float pos[2] = { 0.0f, 1.0f };
    const pixel_t cols[2] = { red, blue };
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

  printf("gradient: only a gradient answers as_gradient()\n");
  {
    const float pos[2] = { 0.0f, 1.0f };
    const pixel_t cols[2] = { red, blue };
    gradient_brush_t g(GRADIENT_LINEAR, 0, 0, 1, 0, pos, cols, 2, nullptr);
    color_brush_t c(rgb_color_t(255, 0, 0, 255));
    brush_t *bg = &g, *bc = &c;
    CHECK(bg->as_gradient() == &g);
    CHECK(bc->as_gradient() == nullptr);
  }

  printf("gradient: the brush stays inside its expected footprint\n");
  {
    // 1KB of it is the LUT. Worth pinning because the menu allocates one of
    // these per icon per frame today.
    CHECK(sizeof(gradient_brush_t) <= 1152);
    CHECK(gradient_brush_t::max_stops == 16);
  }
}
