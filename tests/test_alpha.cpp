// image_t::alpha() is the global alpha everything blended *into* that image is
// weighted by. Every brush that paints a new colour observes it; the effect
// brushes that rewrite the pixel they read do not, and neither does a source
// image, which carries no alpha of its own.
//
// Each brush is checked for the same three things: 255 leaves the render exactly
// as an opaque one, 0 leaves the destination untouched, and 128 lands between the
// two. Where the arithmetic is exactly known the literal is asserted outright.

#include "test.hpp"
#include "helpers.hpp"

using namespace picovector;
using namespace pvtest;

namespace {

  const uint32_t BLACK = 0xff000000u;
  const uint32_t WHITE = 0xffffffffu;

  // White over opaque black at half alpha, which every paint brush should agree
  // on when its source resolves to white.
  const uint32_t HALF_WHITE = 0xff808080u;

  // Fill the whole canvas with `b` through the solid span path.
  uint32_t solid(canvas_t &c, brush_t *b, uint32_t alpha) {
    c.flat(BLACK);
    c.img.alpha((uint8_t)alpha);
    c.img.antialias(OFF);
    c.img.brush(b);
    c.img.rectangle(c.img.bounds());
    c.img.alpha(255);
    return c.at(c.w / 2, c.h / 2);
  }

  // The same through the coverage-masked (AA) path. A rectangle on integer
  // bounds is fully covered in its interior, so the centre pixel must match the
  // solid path exactly.
  uint32_t masked(canvas_t &c, brush_t *b, uint32_t alpha) {
    c.flat(BLACK);
    c.img.alpha((uint8_t)alpha);
    c.img.antialias(X4);
    c.img.brush(b);
    shape_t *s = rectangle(0, 0, (float)c.w, (float)c.h);
    c.img.shape(s);
    c.img.antialias(OFF);
    c.img.alpha(255);
    return c.at(c.w / 2, c.h / 2);
  }

  // Compare colour only. Compositing a partly-transparent buffer over an opaque
  // background raises the result's alpha to 255, so the alpha channel is not the
  // thing under test when the question is "how much colour arrived".
  bool rgb_eq(uint32_t a, uint32_t b) { return (a & 0x00ffffffu) == (b & 0x00ffffffu); }

  // Does channel-wise fading hold: 128 sits between 0 (the background) and 255?
  bool between(uint32_t lo, uint32_t mid, uint32_t hi) {
    for(int i = 0; i < 3; i++) {
      uint32_t l = (lo >> (i * 8)) & 0xff, m = (mid >> (i * 8)) & 0xff, h = (hi >> (i * 8)) & 0xff;
      uint32_t min_c = l < h ? l : h, max_c = l < h ? h : l;
      if(m + 1 < min_c || m > max_c + 1) return false;
    }
    return true;
  }

  // Check one brush against all three alphas, on both span paths.
  void check_brush(const char *name, brush_t *b) {
    canvas_t c(16, 16);

    uint32_t opaque = solid(c, b, 255);
    uint32_t half   = solid(c, b, 128);
    uint32_t none   = solid(c, b, 0);

    CHECK_MSG(none == BLACK, name);                       // alpha 0 writes nothing
    CHECK_MSG(opaque != BLACK, name);                     // ...and 255 definitely writes
    CHECK_MSG(half != opaque, name);                      // ...and 128 is not 255
    CHECK_MSG(between(BLACK, half, opaque), name);        // ...it sits between them

    // The masked path agrees with the solid one wherever coverage is full.
    CHECK_MSG(masked(c, b, 255) == opaque, name);
    CHECK_MSG(masked(c, b, 128) == half, name);
    CHECK_MSG(masked(c, b, 0) == BLACK, name);
  }

}

void test_alpha() {
  printf("alpha: a solid pen is weighted by the target's global alpha\n");
  {
    canvas_t c(16, 16);
    color_brush_t pen(rgb_color_t(255, 255, 255, 255));

    CHECK(solid(c, &pen, 255) == WHITE);
    CHECK(solid(c, &pen, 128) == HALF_WHITE);
    CHECK(solid(c, &pen, 0) == BLACK);
    CHECK(masked(c, &pen, 128) == HALF_WHITE);
    check_brush("color", &pen);
  }

  printf("alpha: a pattern's two colours are both weighted\n");
  {
    // Pattern 5 is a half-and-half checker, so both colours land in a 16x16 fill.
    pattern_brush_t pat(rgb_color_t(255, 255, 255, 255), rgb_color_t(255, 255, 255, 255), 5);
    check_brush("pattern", &pat);

    canvas_t c(16, 16);
    CHECK(solid(c, &pat, 128) == HALF_WHITE);   // both colours white: same answer
  }

  printf("alpha: a texture brush is weighted, and the texture's own alpha is not\n");
  {
    image_t tex(8, 8, RGBA8888, false, 0);
    for(int y = 0; y < 8; y++)
      for(int x = 0; x < 8; x++) ((uint32_t *)tex.ptr(0, y))[x] = WHITE;

    image_brush_t ib(&tex);
    check_brush("image", &ib);

    canvas_t c(16, 16);
    CHECK(solid(c, &ib, 128) == HALF_WHITE);

    // A source image carries no alpha: setting it changes nothing.
    uint32_t reference = solid(c, &ib, 255);
    tex.alpha(1);
    CHECK_MSG(solid(c, &ib, 255) == reference, "a texture's own alpha weighted a brush");
    tex.alpha(128);
    CHECK_MSG(solid(c, &ib, 255) == reference, "a texture's own alpha weighted a brush");
    tex.alpha(255);
  }

  printf("alpha: a gradient is weighted, on all three geometries\n");
  {
    float pos[2] = { 0.0f, 1.0f };
    color_t cols[2] = { rgb_color_t(255, 255, 255, 255), rgb_color_t(255, 255, 255, 255) };

    for(int type = 0; type < 3; type++) {
      gradient_brush_t g(type, 0, 0, 16, 0, pos, cols, 2, nullptr);
      check_brush("gradient", &g);
      canvas_t c(16, 16);
      CHECK(solid(c, &g, 128) == HALF_WHITE);   // a flat white ramp, so exact
    }
  }

  printf("alpha: a fractal brush is weighted\n");
  {
    fractal_brush_t f(8.0f, 2, 0.5f, 4, 1234u, nullptr);
    float pos[2] = { 0.0f, 1.0f };
    color_t cols[2] = { rgb_color_t(255, 255, 255, 255), rgb_color_t(255, 255, 255, 255) };
    f.ramp(pos, cols, 2);
    check_brush("fractal", &f);

    canvas_t c(16, 16);
    CHECK(solid(c, &f, 128) == HALF_WHITE);     // a flat white ramp, so exact
  }

  printf("alpha: erase is a partial erase at a partial alpha\n");
  {
    // erase lerps toward its tint rather than compositing over it, so alpha
    // weights the lerp. At 255 it must still land on the tint exactly.
    canvas_t c(16, 16);
    transparent_brush_t erase;

    CHECK(solid(c, &erase, 255) == 0u);            // fully erased
    CHECK(solid(c, &erase, 0) == BLACK);           // untouched
    uint32_t half = solid(c, &erase, 128);
    CHECK(half != 0u && half != BLACK);            // partly erased
    CHECK(between(BLACK, half, 0u));

    CHECK(masked(c, &erase, 255) == 0u);
    CHECK(masked(c, &erase, 128) == half);
    CHECK(masked(c, &erase, 0) == BLACK);

    // A colour tint lerps toward that colour instead of toward transparent.
    transparent_brush_t tint(rgb_color_t(255, 255, 255, 255));
    CHECK(solid(c, &tint, 255) == WHITE);
    CHECK(solid(c, &tint, 0) == BLACK);
    CHECK(between(BLACK, solid(c, &tint, 128), WHITE));
  }

  printf("alpha: an effect brush ignores it, having its own strength\n");
  {
    // These read the destination pixel and write it back rather than compositing
    // a new colour onto it, so the global alpha does not apply.
    canvas_t c(16, 16);
    invert_brush_t inv;

    c.flat(0xff204060u);
    c.img.alpha(128);
    c.img.antialias(OFF);
    c.img.brush(&inv);
    c.img.rectangle(c.img.bounds());
    uint32_t at_half = c.at(8, 8);
    c.img.alpha(255);

    c.flat(0xff204060u);
    c.img.brush(&inv);
    c.img.rectangle(c.img.bounds());
    CHECK_MSG(c.at(8, 8) == at_half, "an effect brush observed the global alpha");
  }

  printf("alpha: an offscreen applies its own alpha once, not again when blitted\n");
  {
    // The case the old two-meanings field got wrong: painting into a buffer with
    // alpha 20 and then compositing it out applied 20/255 twice.
    canvas_t buf(16, 16);
    color_brush_t pen(rgb_color_t(255, 255, 255, 255));

    uint32_t painted = solid(buf, &pen, 20);      // 20 applied here, once

    canvas_t screen(16, 16);
    screen.flat(BLACK);
    screen.img.alpha(255);                        // and not again here
    buf.img.alpha(20);                            // the source's alpha is ignored
    buf.img.blit(&screen.img, vec2_t(0, 0));
    CHECK_MSG(rgb_eq(screen.at(8, 8), painted), "an offscreen's alpha was applied twice");
    buf.img.alpha(255);

    // Compositing it out at a partial alpha is the target's job, and that is
    // where group opacity comes from.
    screen.flat(BLACK);
    screen.img.alpha(128);
    buf.img.blit(&screen.img, vec2_t(0, 0));
    screen.img.alpha(255);
    CHECK(between(BLACK, screen.at(8, 8), painted));
  }
}
