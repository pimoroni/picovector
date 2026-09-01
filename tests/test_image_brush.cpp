// The texture brush tiles, so its filtered taps have to wrap where
// image_t::sample() clamps. A clamped tap shows up as a hard step at every tile
// boundary, which is what these tests look for: a magnified two-colour texture
// must ramp across the wrap as smoothly as it does inside the tile.

#include "test.hpp"
#include "helpers.hpp"

using namespace picovector;
using namespace pvtest;

namespace {

  const uint32_t BLACK = 0xff000000u;
  const uint32_t WHITE = 0xffffffffu;

  const int MAG = 8;      // texture magnification
  const int PERIOD = 2 * MAG;

  // 2x2, left column black and right column white: one hard edge inside the
  // tile and one across the wrap, so both are visible in a single row.
  void two_column(image_t &tex) {
    for(int y = 0; y < 2; y++) {
      ((uint32_t *)tex.ptr(0, y))[0] = BLACK;
      ((uint32_t *)tex.ptr(0, y))[1] = WHITE;
    }
  }

  // Fill the canvas with the brush and return the middle row's blue channel.
  std::vector<int> row(canvas_t &c, brush_t *b) {
    c.flat(0xff7f0000u);
    c.img.antialias(OFF);
    c.img.brush(b);
    c.img.rectangle(c.img.bounds());
    std::vector<int> v(c.w);
    for(int x = 0; x < c.w; x++) v[x] = (c.at(x, c.h / 2) >> 16) & 0xff;
    return v;
  }

  int largest_step(const std::vector<int> &v) {
    int worst = 0;
    for(size_t i = 1; i < v.size(); i++) {
      int d = v[i] - v[i - 1];
      if(d < 0) d = -d;
      if(d > worst) worst = d;
    }
    return worst;
  }

  int distinct(const std::vector<int> &v) {
    bool seen[256] = {false};
    int n = 0;
    for(int x : v) if(!seen[x]) { seen[x] = true; n++; }
    return n;
  }

}

void test_image_brush() {
  printf("image brush: NEAREST keeps its hard texel edges\n");
  {
    image_t tex(2, 2, RGBA8888, false, 0);
    two_column(tex);
    mat3_t t; t.scale((float)MAG);
    image_brush_t ib(&tex, &t, NEAREST);

    canvas_t c(64, 8);
    std::vector<int> v = row(c, &ib);
    CHECK(distinct(v) == 2);
    CHECK(largest_step(v) == 255);
  }

  printf("image brush: BILINEAR ramps across the tile wrap, not just inside it\n");
  {
    image_t tex(2, 2, RGBA8888, false, 0);
    two_column(tex);
    mat3_t t; t.scale((float)MAG);
    image_brush_t ib(&tex, &t, BILINEAR);

    canvas_t c(64, 8);
    std::vector<int> v = row(c, &ib);
    // A clamped tap holds the last texel and then jumps the full range; a
    // wrapped one steps by about 255/MAG.
    CHECK_MSG(largest_step(v) < 255 / MAG + 8, "seam at a tile boundary");
    CHECK(distinct(v) > 2);

    // Both halves of a period carry the ramp: the one inside the tile and the
    // one across the wrap.
    int lo = 0, hi = 0;
    for(int x = 0; x < PERIOD; x++) {
      int mid = v[x] > 8 && v[x] < 247;
      (x < MAG ? lo : hi) += mid;
    }
    CHECK(lo > 0);
    CHECK_MSG(hi > 0, "wrap half is flat");
  }

  printf("image brush: BICUBIC wraps too\n");
  {
    image_t tex(2, 2, RGBA8888, false, 0);
    two_column(tex);
    mat3_t t; t.scale((float)MAG);
    image_brush_t ib(&tex, &t, BICUBIC);

    canvas_t c(64, 8);
    std::vector<int> v = row(c, &ib);
    CHECK_MSG(largest_step(v) < 128, "seam at a tile boundary");
    CHECK(distinct(v) > 2);
  }

  printf("image brush: tiling stays periodic under every filter\n");
  {
    image_t tex(2, 2, RGBA8888, false, 0);
    two_column(tex);
    filter_t filters[3] = {NEAREST, BILINEAR, BICUBIC};
    for(filter_t f : filters) {
      mat3_t t; t.scale((float)MAG);
      image_brush_t ib(&tex, &t, f);
      canvas_t c(64, 8);
      std::vector<int> v = row(c, &ib);
      int mismatched = 0;
      for(int x = 0; x + PERIOD < c.w; x++) if(v[x] != v[x + PERIOD]) mismatched++;
      CHECK(mismatched == 0);
    }
  }

  printf("image brush: a palette source interpolates its colours, not its indices\n");
  {
    image_t tex(2, 2, RGBA8888, true, 2);
    tex.palette(0, BLACK);
    tex.palette(1, WHITE);
    for(int y = 0; y < 2; y++) {
      ((uint8_t *)tex.ptr(0, y))[0] = 0;
      ((uint8_t *)tex.ptr(0, y))[1] = 1;
    }

    mat3_t t; t.scale((float)MAG);
    image_brush_t ib(&tex, &t, BILINEAR);

    canvas_t c(64, 8);
    std::vector<int> v = row(c, &ib);
    // Interpolating indices would top out at 1, so the row would be black.
    CHECK(distinct(v) > 2);
    CHECK_MSG(largest_step(v) < 255 / MAG + 8, "seam at a tile boundary");
    int white = 0;
    for(int x : v) if(x == 255) white++;
    CHECK(white > 0);
  }
}
