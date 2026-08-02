// Blitting: placement, scaling, and the bounds every blit has to respect.
//
// The canary test is the one that matters: blit_hspan/blit_vspan used to clip
// only along the axis they travelled, so a span positioned outside the image on
// the other axis wrote past the end of the buffer. Both are reachable from
// Python with a caller-supplied position.

#include "test.hpp"
#include "helpers.hpp"

using namespace picovector;
using namespace pvtest;

// An image wrapped over the middle of a larger arena, so a write outside its
// bounds lands in a canary region instead of somewhere unpredictable.
struct guarded_image_t {
  static const int PAD = 4096;
  std::vector<uint32_t> arena;
  image_t img;
  int w, h;

  guarded_image_t(int w, int h)
    : arena((size_t)PAD * 2 + (size_t)w * h, 0xDEADBEEFu),
      img(arena.data() + PAD, w, h), w(w), h(h) {}

  int canary_writes() const {
    int n = 0;
    for(int i = 0; i < PAD; i++) if(arena[i] != 0xDEADBEEFu) n++;
    for(size_t i = PAD + (size_t)w * h; i < arena.size(); i++) if(arena[i] != 0xDEADBEEFu) n++;
    return n;
  }
};

void test_blit() {
  canvas_t src_c(16, 16);
  src_c.flat(0xff00ff00u);
  image_t *src = &src_c.img;

  printf("blit: a span outside the image writes nothing outside the buffer\n");
  for(int dy = -8; dy <= 8; dy += 8) {
    guarded_image_t g(32, 32);
    src->blit_hspan(&g.img, vec2_t(0, dy < 0 ? dy : 32 + dy), 16, vec2_t(0, 0), vec2_t(15, 15));
    CHECK(g.canary_writes() == 0);
  }
  for(int dx = -8; dx <= 8; dx += 8) {
    guarded_image_t g(32, 32);
    src->blit_vspan(&g.img, vec2_t(dx < 0 ? dx : 32 + dx, 0), 16, vec2_t(0, 0), vec2_t(15, 15));
    CHECK(g.canary_writes() == 0);
  }
  {
    // far outside, both axes, both directions
    guarded_image_t g(32, 32);
    src->blit_hspan(&g.img, vec2_t(-500, 999), 64, vec2_t(0, 0), vec2_t(15, 15));
    src->blit_vspan(&g.img, vec2_t(999, -500), 64, vec2_t(0, 0), vec2_t(15, 15));
    src->blit(&g.img, vec2_t(-100, -100));
    src->blit(&g.img, vec2_t(900, 900));
    src->blit(&g.img, rect_t(-40, -40, 200, 200));
    CHECK(g.canary_writes() == 0);
  }

  printf("blit: places pixels where asked\n");
  {
    canvas_t dst(64, 64);
    dst.flat(0xff000000u);
    dst.snapshot();
    src->blit(&dst.img, vec2_t(10, 12));
    CHECK(dst.at(10, 12) == 0xff00ff00u);
    CHECK(dst.at(25, 27) == 0xff00ff00u);      // 16x16 from (10,12)
    CHECK(dst.at(9, 12) == 0xff000000u);       // one left of it, untouched
    CHECK(dst.at(26, 27) == 0xff000000u);
    CHECK(dst.changed_total() == 16 * 16);
  }

  printf("blit: partially offscreen keeps the visible part\n");
  {
    canvas_t dst(64, 64);
    dst.flat(0xff000000u);
    dst.snapshot();
    src->blit(&dst.img, vec2_t(-8, -8));
    CHECK(dst.changed_total() == 8 * 8);
    CHECK(dst.at(0, 0) == 0xff00ff00u);
    CHECK(dst.at(8, 8) == 0xff000000u);
  }

  printf("blit: fully offscreen draws nothing\n");
  {
    canvas_t dst(64, 64);
    dst.snapshot();
    src->blit(&dst.img, vec2_t(-16, 0));
    src->blit(&dst.img, vec2_t(64, 0));
    src->blit(&dst.img, vec2_t(0, -16));
    src->blit(&dst.img, vec2_t(0, 64));
    CHECK(dst.changed_total() == 0);
  }

  printf("blit: scaling covers the target rect\n");
  {
    canvas_t dst(64, 64);
    dst.flat(0xff000000u);
    dst.snapshot();
    src->blit(&dst.img, rect_t(0, 0, 16, 16), rect_t(8, 8, 32, 32));
    CHECK(dst.changed_total() == 32 * 32);
    CHECK(dst.at(8, 8) == 0xff00ff00u);
    CHECK(dst.at(39, 39) == 0xff00ff00u);
    CHECK(dst.at(7, 7) == 0xff000000u);
  }

  printf("blit: a span draws its length\n");
  {
    canvas_t dst(64, 64);
    dst.flat(0xff000000u);
    dst.snapshot();
    src->blit_hspan(&dst.img, vec2_t(4, 20), 32, vec2_t(0, 0), vec2_t(15, 15));
    CHECK(dst.changed_total() == 32);
    CHECK(dst.changed(4, 20));
    CHECK(!dst.changed(3, 20));
    CHECK(!dst.changed(36, 20));
  }

  // ── from an indexed source ────────────────────────────────────────────────
  // Every shipped indexed PNG and every GIF goes through these two paths, and
  // neither had a test. span_blit takes the palette as an argument; the scaled
  // and span variants build their own src_pal from src->palette_data().
  //
  // A 16x16 source of four colours, left half index 1 and right half index 2,
  // so a flip is visible and a scale has an edge to land on.
  image_t pal_src(16, 16, RGBA8888, true, 4);
  const uint32_t PAL_RED = rgb_color_t(255, 0, 0, 255)._p;
  const uint32_t PAL_BLUE = rgb_color_t(0, 0, 255, 255)._p;
  pal_src.palette(0, 0);
  pal_src.palette(1, PAL_RED);
  pal_src.palette(2, PAL_BLUE);
  for(int y = 0; y < 16; y++)
    for(int x = 0; x < 16; x++)
      *((uint8_t *)pal_src.ptr(x, y)) = x < 8 ? 1 : 2;

  printf("blit: an indexed source resolves through its palette\n");
  {
    canvas_t dst(64, 64);
    dst.flat(0xff000000u);
    dst.snapshot();
    pal_src.blit(&dst.img, vec2_t(4, 4));
    CHECK(dst.changed_total() == 16 * 16);
    CHECK(dst.at(4, 4) == PAL_RED);
    CHECK(dst.at(11, 4) == PAL_RED);
    CHECK(dst.at(12, 4) == PAL_BLUE);
    CHECK(dst.at(19, 19) == PAL_BLUE);
  }

  printf("blit: writing a palette entry recolours the next blit\n");
  {
    canvas_t dst(64, 64);
    dst.flat(0xff000000u);
    dst.snapshot();
    const uint32_t PAL_GREEN = rgb_color_t(0, 255, 0, 255)._p;
    pal_src.palette(1, PAL_GREEN);
    pal_src.blit(&dst.img, vec2_t(0, 0));
    CHECK(dst.at(0, 0) == PAL_GREEN);
    CHECK(dst.at(8, 0) == PAL_BLUE);          // the entry not written is untouched
    pal_src.palette(1, PAL_RED);
  }

  printf("blit: an indexed source scales, and samples NEAREST whatever is asked\n");
  {
    // BILINEAR would interpolate indices if the palette branch were missed, so
    // the halves would blur into a purple seam instead of meeting cleanly.
    canvas_t dst(64, 64);
    dst.flat(0xff000000u);
    dst.snapshot();
    pal_src.blit(&dst.img, rect_t(0, 0, 16, 16), rect_t(0, 0, 32, 32), BILINEAR);
    CHECK(dst.changed_total() == 32 * 32);
    CHECK(dst.at(0, 0) == PAL_RED);
    CHECK(dst.at(15, 31) == PAL_RED);
    CHECK(dst.at(16, 0) == PAL_BLUE);
    CHECK(dst.at(31, 31) == PAL_BLUE);
  }

  printf("blit: an indexed source flips\n");
  {
    canvas_t dst(64, 64);
    dst.flat(0xff000000u);
    dst.snapshot();
    pal_src.blit(&dst.img, rect_t(0, 0, 16, 16), rect_t(0, 0, -16, 16));
    CHECK(dst.at(0, 0) == PAL_BLUE);          // the halves swap
    CHECK(dst.at(15, 0) == PAL_RED);
  }

  printf("blit: an indexed source stays inside the buffer\n");
  {
    guarded_image_t g(32, 32);
    pal_src.blit(&g.img, vec2_t(-8, -8));
    pal_src.blit(&g.img, vec2_t(28, 28));
    pal_src.blit(&g.img, rect_t(0, 0, 16, 16), rect_t(-20, -20, 80, 80));
    pal_src.blit_hspan(&g.img, vec2_t(-500, 999), 64, vec2_t(0, 0), vec2_t(15, 15));
    pal_src.blit_vspan(&g.img, vec2_t(999, -500), 64, vec2_t(0, 0), vec2_t(15, 15));
    CHECK(g.canary_writes() == 0);
  }

  printf("blit: an indexed image is refused as a target\n");
  {
    // One byte a pixel against a blit that writes four: this would run four
    // times past the end of every row. The bindings keep it unreachable by
    // giving a palettised buffer a type with no blit, but the core refuses too.
    image_t pal_dst(16, 16, RGBA8888, true, 4);
    for(int i = 0; i < 16 * 16; i++) ((uint8_t *)pal_dst.ptr(0, 0))[i] = 0;
    src->blit(&pal_dst, vec2_t(0, 0));
    src->blit(&pal_dst, rect_t(0, 0, 16, 16), rect_t(0, 0, 16, 16));
    int written = 0;
    for(int i = 0; i < 16 * 16; i++) if(((uint8_t *)pal_dst.ptr(0, 0))[i] != 0) written++;
    CHECK(written == 0);
  }
}
