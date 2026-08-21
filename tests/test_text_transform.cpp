// image.text()'s mat3 transform: what it maps, what it leaves alone, and that
// the untransformed path is untouched by its presence.

#include <vector>

#include "test.hpp"
#include "helpers.hpp"
#include "font.hpp"
#include "pixel_font.hpp"

using namespace picovector;
using namespace pvtest;

// A pixel font with one glyph: a solid GW x GH block, so a placement can be
// checked by the box it fills rather than by a glyph shape.
static const int GW = 5, GH = 7;

struct block_font_t {
  pixel_font_t font;
  pixel_font_glyph_t glyph{'A', GW};
  std::vector<uint8_t> data;

  block_font_t() {
    uint32_t bytes_per_row = (8 + 7) >> 3;
    data.assign(bytes_per_row * GH, 0);
    for(int y = 0; y < GH; y++)
      for(int x = 0; x < GW; x++) data[y * bytes_per_row + (x >> 3)] |= 0x80u >> (x & 7);
    font.glyph_count = 1;
    font.glyph_data_size = bytes_per_row * GH;
    font.width = 8;
    font.height = GH;
    font.glyphs = &glyph;
    font.glyph_data = data.data();
    font.name[0] = 0;
  }
};

// Draw "A" at (x, y) with the given transform, returning the canvas.
static void draw_block(ink_canvas_t &c, pixel_font_t *f, float x, float y, int scale,
                       const mat3_t *transform) {
  c.img.pixel_font(f);
  text_cursor_t *cur = c.img.text_cursor_state();
  cur->x = x; cur->y = y; cur->origin_x = x; cur->valid = true;
  f->draw(&c.img, "A", scale, transform);
}

// The .af from test_font.cpp's builder is not reachable from here, so this makes
// its own: one glyph 'A', a single square contour, 128 units per em.
struct square_font_t {
  font_t font;
  glyph_t glyph{};
  glyph_path_t path{};
  glyph_path_point_t points[4];

  square_font_t() {
    points[0] = {0, 0}; points[1] = {64, 0}; points[2] = {64, 64}; points[3] = {0, 64};
    path.point_count = 4;
    path.points = points;
    glyph.codepoint = 'A';
    glyph.x = 0; glyph.y = 0; glyph.w = 64; glyph.h = 64;
    glyph.advance = 64;
    glyph.path_count = 1;
    glyph.paths = &path;
    font.glyph_count = 1;
    font.glyphs = &glyph;
    font.units_per_em = 128.0f;
    font.wide_points = false;
  }
};

void test_text_transform() {
  block_font_t bf;

  printf("text transform: an identity mat3 matches the untransformed blit\n");
  for(int scale = 1; scale <= 3; scale++) {
    ink_canvas_t plain(48, 48, OFF), mapped(48, 48, OFF);
    mat3_t identity;
    draw_block(plain, &bf.font, 6, 5, scale, nullptr);
    draw_block(mapped, &bf.font, 6, 5, scale, &identity);
    int differing = 0;
    for(int y = 0; y < 48; y++)
      for(int x = 0; x < 48; x++) if(plain.at(x, y) != mapped.at(x, y)) differing++;
    CHECK(differing == 0);
    CHECK(measure_ink(plain).full == GW * GH * scale * scale);
  }

  printf("text transform: translating the matrix moves the drawn glyph\n");
  {
    ink_canvas_t moved(48, 48, OFF), placed(48, 48, OFF);
    mat3_t t;
    t.translate(9, 4);
    draw_block(moved, &bf.font, 6, 5, 1, &t);
    draw_block(placed, &bf.font, 15, 9, 1, nullptr);
    int differing = 0;
    for(int y = 0; y < 48; y++)
      for(int x = 0; x < 48; x++) if(moved.at(x, y) != placed.at(x, y)) differing++;
    CHECK(differing == 0);
  }

  printf("text transform: scaling the matrix matches the integer scale\n");
  {
    ink_canvas_t mapped(48, 48, OFF), scaled(48, 48, OFF);
    mat3_t t;
    t.scale(2.0f, 2.0f);
    draw_block(mapped, &bf.font, 3, 2, 1, &t);   // caret is scaled too: 3,2 -> 6,4
    draw_block(scaled, &bf.font, 6, 4, 2, nullptr);
    int differing = 0;
    for(int y = 0; y < 48; y++)
      for(int x = 0; x < 48; x++) if(mapped.at(x, y) != scaled.at(x, y)) differing++;
    CHECK(differing == 0);
  }

  printf("text transform: a quarter turn transposes the glyph box\n");
  {
    ink_canvas_t c(48, 48, OFF);
    // Rotate about the caret: translate there, turn, translate back.
    mat3_t t;
    t.translate(20, 20);
    t.rotate(90);
    t.translate(-20, -20);
    draw_block(c, &bf.font, 20, 20, 1, &t);
    ink_t k = measure_ink(c);
    // The block spans GW across and GH down before the turn, so after it the
    // inked box is GH across and GW down.
    CHECK(k.maxx - k.minx + 1 == GH);
    CHECK(k.maxy - k.miny + 1 == GW);
    CHECK(k.full == GW * GH);
  }

  printf("text transform: the caret advances in untransformed text space\n");
  {
    ink_canvas_t a(48, 48, OFF), b(48, 48, OFF);
    mat3_t t;
    t.rotate(30);
    draw_block(a, &bf.font, 6, 5, 1, nullptr);
    draw_block(b, &bf.font, 6, 5, 1, &t);
    CHECK(a.img.text_cursor_state()->x == b.img.text_cursor_state()->x);
    CHECK(a.img.text_cursor_state()->y == b.img.text_cursor_state()->y);
  }

  printf("text transform: a singular matrix draws nothing\n");
  {
    ink_canvas_t c(48, 48, OFF);
    mat3_t t;
    t.scale(0.0f, 0.0f);
    draw_block(c, &bf.font, 6, 5, 1, &t);
    CHECK(measure_ink(c).inked == 0);
  }

  printf("text transform: an absurd scale is clipped, not overflowed\n");
  {
    ink_canvas_t c(48, 48, OFF);
    mat3_t t;
    t.scale(1e9f, 1e9f);
    draw_block(c, &bf.font, 0, 0, 1, &t);
    // The glyph covers the whole canvas rather than running off an int.
    CHECK(measure_ink(c).full == 48 * 48);
  }

  printf("text transform: a NaN in the matrix draws nothing\n");
  {
    ink_canvas_t c(48, 48, OFF);
    mat3_t t;
    t.v02 = 0.0f / 0.0f;
    draw_block(c, &bf.font, 6, 5, 1, &t);
    CHECK(measure_ink(c).inked == 0);
  }

  printf("text transform: a transformed glyph still respects the clip\n");
  {
    ink_canvas_t c(48, 48, OFF);
    c.img.clip(rect_t(0, 0, 10, 48));
    mat3_t t;
    t.translate(20, 0);
    draw_block(c, &bf.font, 6, 5, 1, &t);
    CHECK(measure_ink(c).inked == 0);
  }

  printf("text transform: a vector glyph maps like a shape\n");
  {
    square_font_t vf;
    ink_canvas_t plain(64, 64, OFF), mapped(64, 64, OFF);
    mat3_t identity;

    plain.img.font(&vf.font);
    text_cursor_t *cur = plain.img.text_cursor_state();
    cur->x = 4; cur->y = 4; cur->origin_x = 4; cur->valid = true;
    vf.font.draw(&plain.img, "A", 32.0f, nullptr);

    mapped.img.font(&vf.font);
    cur = mapped.img.text_cursor_state();
    cur->x = 4; cur->y = 4; cur->origin_x = 4; cur->valid = true;
    vf.font.draw(&mapped.img, "A", 32.0f, &identity);

    ink_t kp = measure_ink(plain), km = measure_ink(mapped);
    CHECK(kp.inked > 0);
    CHECK(kp.inked == km.inked && kp.minx == km.minx && kp.miny == km.miny);

    // Half scale about the origin halves the box the glyph covers.
    ink_canvas_t half(64, 64, OFF);
    mat3_t t;
    t.scale(0.5f, 0.5f);
    half.img.font(&vf.font);
    cur = half.img.text_cursor_state();
    cur->x = 4; cur->y = 4; cur->origin_x = 4; cur->valid = true;
    vf.font.draw(&half.img, "A", 32.0f, &t);
    ink_t kh = measure_ink(half);
    int plain_w = kp.maxx - kp.minx + 1, half_w = kh.maxx - kh.minx + 1;
    CHECK(half_w * 2 >= plain_w - 1 && half_w * 2 <= plain_w + 1);
    CHECK(kh.minx * 2 >= kp.minx - 1 && kh.minx * 2 <= kp.minx + 1);
  }
}
