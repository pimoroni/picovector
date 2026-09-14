// Output-hash oracle for every brush and filter.
//
// The optimisations these guard are meant to be output-identical: a faster loop
// that computes the same pixels. A hash over the whole canvas is the cheapest
// way to say so, and it catches a changed rounding in one channel of one pixel
// where a targeted test would not.
//
// Every brush is rendered twice, through the solid span path with antialiasing
// off and through the masked path with a circle at X4, because the two are
// separate loops in every brush and an optimisation usually touches both. The
// cases deliberately use spans that do not start at x=0 and sizes that do not
// divide the canvas, so block and wrap arithmetic has to be right at the ends
// as well as in the middle.
//
// To rebase after a deliberate output change, run with PV_ORACLE_PRINT=1 and
// paste the printed table back in.

#include <cstdlib>

#include "test.hpp"
#include "helpers.hpp"

using namespace picovector;
using namespace pvtest;

namespace {

  uint64_t hash_canvas(const canvas_t &c) {
    uint64_t h = 1469598103934665603ull;
    for(int i = 0; i < c.w * c.h; i++) {
      uint32_t p = c.pixels[(size_t)i];
      for(int b = 0; b < 32; b += 8) {
        h ^= (uint64_t)((p >> b) & 0xff);
        h *= 1099511628211ull;
      }
    }
    return h;
  }

  struct expected_t { const char *name; uint64_t hash; };

  const expected_t EXPECTED[] = {
#include "brush_oracle_hashes.inc"
  };

  const size_t EXPECTED_COUNT = sizeof(EXPECTED) / sizeof(EXPECTED[0]);

  size_t next_case = 0;
  bool printing = false;

  void record_hash(const char *name, const canvas_t &c) {
    // A case that writes nothing would hash stably forever and prove nothing,
    // so the oracle checks it drew before it checks what it drew.
    if(!printing) CHECK_MSG(c.changed_total() > 0, name);
    uint64_t h = hash_canvas(c);
    if(printing) {
      printf("  { \"%s\", 0x%016llxull },\n", name, (unsigned long long)h);
      next_case++;
      return;
    }
    if(next_case >= EXPECTED_COUNT) {
      CHECK_MSG(false, name);
      next_case++;
      return;
    }
    const expected_t &e = EXPECTED[next_case++];
    char detail[160];
    snprintf(detail, sizeof(detail), "%s: got 0x%016llx want 0x%016llx",
             name, (unsigned long long)h, (unsigned long long)e.hash);
    ::pvtest::record(h == e.hash && strcmp(name, e.name) == 0,
                     "oracle hash", __FILE__, __LINE__, detail);
  }

  // canvas_t owns its pixels in a vector and its image_t points into that
  // vector, so it cannot be returned by value. Every case runs against a fresh
  // one built in place: 67x53, a size sharing no factor with any block or tile
  // below, and every pixel distinct so a brush that only rewrites what it reads
  // still shows up in the hash.
  template<typename F>
  void with_canvas(const char *name, F fn) {
    canvas_t c(67, 53);
    c.img.alpha(255);
    fn(c);
    record_hash(name, c);
  }

  // Solid span path: antialiasing off, a rectangle inset by a prime so spans
  // start and end off any power-of-two boundary.
  void solid(const char *name, brush_t *b) {
    with_canvas(name, [&](canvas_t &c) {
      c.img.antialias(OFF);
      c.img.brush(b);
      c.img.rectangle(rect_t(3, 5, 61, 43));
    });
  }

  // Masked span path: a circle at X4 leaves a fringe of short partly covered
  // spans, which is where per-span setup and end-of-row clamps go wrong.
  void masked(const char *name, brush_t *b) {
    with_canvas(name, [&](canvas_t &c) {
      c.img.antialias(X4);
      c.img.brush(b);
      c.img.circle(vec2_t(33, 26), 23);
    });
  }

  void both(const char *name, brush_t *b) {
    char buf[96];
    snprintf(buf, sizeof(buf), "%s solid", name);
    solid(buf, b);
    snprintf(buf, sizeof(buf), "%s masked", name);
    masked(buf, b);
  }

  // A texture whose size shares no factor with the canvas, so the brush's wrap
  // lands mid-tile on most rows, and with a colour per texel so a tap taken
  // from the wrong one shows.
  struct texture_t {
    std::vector<uint32_t> pixels;
    image_t img;
    texture_t(int w, int h) : pixels((size_t)w * h), img(pixels.data(), w, h) {
      for(int i = 0; i < w * h; i++)
        pixels[i] = 0xff000000u | (uint32_t)((i * 53 + 7) & 0xff)
                  | ((uint32_t)((i * 149 + 31) & 0xff) << 8)
                  | ((uint32_t)((i * 211 + 97) & 0xff) << 16);
    }
  };

}

void test_brush_oracle() {
  printf("brush oracle\n");
  printing = getenv("PV_ORACLE_PRINT") != nullptr;
  if(printing) printf("// regenerated table follows\n");

  // ── colour, pattern, transparent ───────────────────────────────────────────
  { color_t c = rgb_color_t(200, 90, 40, 255); color_brush_t b(c); both("color opaque", &b); }
  { color_t c = rgb_color_t(200, 90, 40, 128); color_brush_t b(c); both("color translucent", &b); }
  { transparent_brush_t b; both("erase", &b); }
  { color_t c = rgb_color_t(10, 200, 90, 96); transparent_brush_t b(c); both("erase colour", &b); }
  { color_t a = rgb_color_t(255, 255, 255, 255), b2 = rgb_color_t(0, 0, 0, 255);
    pattern_brush_t b(a, b2, 17); both("pattern", &b); }

  // ── image brush, every filter, with and without a transform ───────────────
  {
    texture_t tex(5, 3);
    { image_brush_t b(&tex.img, NEAREST); both("image nearest", &b); }
    { image_brush_t b(&tex.img, BILINEAR); both("image bilinear", &b); }
    { image_brush_t b(&tex.img, BICUBIC); both("image bicubic", &b); }
    // A scale-only transform keeps the walk axis-aligned, which is the case
    // worth hoisting the row pointer out of, and magnifies enough that the
    // three filters no longer agree the way they do at 1:1.
    mat3_t axis;
    axis.scale(6.0f, 4.0f);
    { image_brush_t b(&tex.img, &axis, NEAREST); both("image nearest scaled", &b); }
    { image_brush_t b(&tex.img, &axis, BILINEAR); both("image bilinear scaled", &b); }
    { image_brush_t b(&tex.img, &axis, BICUBIC); both("image bicubic scaled", &b); }
    // A rotation puts the sample walk on both axes at once and drives the
    // source coordinate negative, which is the case a naive wrap gets wrong.
    mat3_t t;
    t.translate(11, 7);
    t.rotate(37);
    t.scale(2.5f, 1.75f);
    { image_brush_t b(&tex.img, &t, NEAREST); both("image nearest transformed", &b); }
    { image_brush_t b(&tex.img, &t, BILINEAR); both("image bilinear transformed", &b); }
    { image_brush_t b(&tex.img, &t, BICUBIC); both("image bicubic transformed", &b); }
  }

  // ── gradients ─────────────────────────────────────────────────────────────
  {
    float offsets[] = { 0.0f, 0.4f, 1.0f };
    color_t stops[] = { rgb_color_t(0, 0, 255, 255),
                        rgb_color_t(0, 255, 0, 255),
                        rgb_color_t(255, 0, 0, 160) };
    for(int type = 0; type < 3; type++) {
      gradient_brush_t b(type, 7, 9, 55, 41, offsets, stops, 3, nullptr);
      char buf[48];
      snprintf(buf, sizeof(buf), "gradient type %d", type);
      both(buf, &b);
    }
  }

  // ── effect brushes ────────────────────────────────────────────────────────
  { pixelate_brush_t b(3); both("pixelate 3", &b); }
  { pixelate_brush_t b(7); both("pixelate 7", &b); }
  { blur_brush_t b(1); both("blur r1", &b); }
  { blur_brush_t b(4); both("blur r4", &b); }
  { brightness_brush_t b(60); both("lighten", &b); }
  { brightness_brush_t b(-60); both("darken", &b); }
  { monochrome_brush_t b; both("monochrome", &b); }
  { dither_brush_t b; both("dither", &b); }
  { invert_brush_t b; both("invert", &b); }
  { color_t lo = rgb_color_t(0, 0, 0, 255), hi = rgb_color_t(255, 255, 255, 255);
    threshold_brush_t b(120, lo, hi); both("threshold", &b); }
  { saturation_brush_t b(90); both("saturation", &b); }
  { contrast_brush_t b(70); both("contrast", &b); }
  { color_t s = rgb_color_t(20, 10, 60, 255), h = rgb_color_t(255, 220, 120, 255);
    duotone_brush_t b(s, h); both("duotone", &b); }
  { crt_brush_t b(3, 90); both("crt", &b); }
  { grid_brush_t b(5, 70); both("grid", &b); }
  { vignette_brush_t b(200); both("vignette", &b); }
  { noise_brush_t b(40, 0); both("noise", &b); }
  { glitch_brush_t b(9); both("glitch", &b); }
  { oilpaint_brush_t b(3, 255); both("oilpaint", &b); }
  { color_t t = rgb_color_t(60, 255, 120, 255); phosphor_brush_t b(t); both("phosphor", &b); }
  { uint32_t pal[] = { 0xff000000u, 0xff4488ccu, 0xffccaa22u, 0xffffffffu };
    palette_dither_brush_t b(pal, 4, 64); both("palette dither", &b); }
  { nightvision_brush_t b; both("nightvision", &b); }
  { chromatic_brush_t b(5); both("chromatic", &b); }
  { fractal_brush_t b(23.0f, 4, 0.5f, 3, 12345u, nullptr);
    float pos[3] = { 0.0f, 0.6f, 1.0f };
    color_t cols[3] = { rgb_color_t(10, 20, 80, 255),
                        rgb_color_t(200, 160, 40, 255),
                        rgb_color_t(255, 255, 255, 255) };
    b.ramp(pos, cols, 3);
    both("fractal", &b); }

  // ── whole-image filters ───────────────────────────────────────────────────
  with_canvas("filter blur", [](canvas_t &c) { c.img.blur(4.0f); });
  with_canvas("filter bloom", [](canvas_t &c) { c.img.bloom(100, 150, 4.0f); });
  with_canvas("filter edgeglow", [](canvas_t &c) { c.img.edgeglow(220); });
  with_canvas("filter wave", [](canvas_t &c) { c.img.wave(4, 4); });
  with_canvas("filter wave bilinear", [](canvas_t &c) { c.img.wave(4, 4, 1.0f, true); });
  with_canvas("filter zoom", [](canvas_t &c) { c.img.zoom(200); });
  with_canvas("filter dither", [](canvas_t &c) { c.img.dither(); });
  with_canvas("filter onebit", [](canvas_t &c) { c.img.onebit(); });
  with_canvas("filter monochrome", [](canvas_t &c) { c.img.monochrome(); });
  with_canvas("filter invert", [](canvas_t &c) { c.img.invert(); });
  with_canvas("filter saturation", [](canvas_t &c) { c.img.saturation(90); });
  with_canvas("filter contrast", [](canvas_t &c) { c.img.contrast(70); });
  with_canvas("filter crt", [](canvas_t &c) { c.img.crt(3, 90); });
  with_canvas("filter grid", [](canvas_t &c) { c.img.grid(5, 70); });
  with_canvas("filter vignette", [](canvas_t &c) { c.img.vignette(200); });
  with_canvas("filter gameboy", [](canvas_t &c) { c.img.gameboy(); });
  with_canvas("filter noise", [](canvas_t &c) { c.img.noise(40, 0); });
  with_canvas("filter glitch", [](canvas_t &c) { c.img.glitch(9); });
  with_canvas("filter oilpaint", [](canvas_t &c) { c.img.oilpaint(3, 255); });
  with_canvas("filter cga", [](canvas_t &c) { c.img.cga(); });
  with_canvas("filter phosphor", [](canvas_t &c) { c.img.phosphor(rgb_color_t(60, 255, 120, 255)); });
  with_canvas("filter synthwave", [](canvas_t &c) { c.img.synthwave(); });
  with_canvas("filter c64", [](canvas_t &c) { c.img.c64(); });
  with_canvas("filter nightvision", [](canvas_t &c) { c.img.nightvision(); });
  with_canvas("filter chromatic", [](canvas_t &c) { c.img.chromatic(5); });

  // ── blits ─────────────────────────────────────────────────────────────────
  {
    texture_t src(23, 19);
    // 1:1, offset so the run is not word-aligned against the source.
    with_canvas("blit 1:1", [&](canvas_t &c) { src.img.blit(&c.img, vec2_t(5, 7)); });
    // Partly off each edge, which is what exercises the clamps at the span ends.
    with_canvas("blit 1:1 clipped", [&](canvas_t &c) { src.img.blit(&c.img, vec2_t(-6, -4)); });
    with_canvas("blit 1:1 clipped far", [&](canvas_t &c) { src.img.blit(&c.img, vec2_t(59, 44)); });
    // Scaled, at each filter, up and down.
    for(int f = 0; f < 3; f++) {
      filter_t filter = (filter_t)f;
      const char *fname = f == 0 ? "nearest" : f == 1 ? "bilinear" : "bicubic";
      char buf[64];
      snprintf(buf, sizeof(buf), "blit up %s", fname);
      with_canvas(buf, [&](canvas_t &c) { src.img.blit(&c.img, rect_t(2, 3, 61, 47), filter); });
      snprintf(buf, sizeof(buf), "blit down %s", fname);
      with_canvas(buf, [&](canvas_t &c) { src.img.blit(&c.img, rect_t(4, 6, 11, 9), filter); });
      snprintf(buf, sizeof(buf), "blit sub %s", fname);
      with_canvas(buf, [&](canvas_t &c) {
        src.img.blit(&c.img, rect_t(3, 2, 17, 15), rect_t(-5, -3, 55, 49), filter); });
    }
  }

  if(printing) {
    printf("// %zu cases\n", next_case);
    return;
  }
  CHECK_MSG(next_case == EXPECTED_COUNT, "case count matches the recorded table");
}
