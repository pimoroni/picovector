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
}
