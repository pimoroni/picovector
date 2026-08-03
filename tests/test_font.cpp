// The .af container parser: both widths, and every way a file can be wrong.

#include <vector>

#include "test.hpp"
#include "picovector.hpp"
#include "font.hpp"

using namespace picovector;

// Builds a .af in memory, so the tests don't depend on a font file.
struct af_builder_t {
  std::vector<uint8_t> out;
  bool wide;

  explicit af_builder_t(bool wide, int units_per_em = 128) : wide(wide) {
    out.insert(out.end(), {'a', 'f', '!', '?'});
    u16(wide ? 0b11 : 0b01);   // 16-bit contour lengths, plus wide when asked
    u16(1);                    // glyphs
    u16(1);                    // contours
    u16(4);                    // points
    if(wide) u16(units_per_em);
    // glyph: codepoint, bbox, advance, contour count
    u16('A');
    coord(2); coord(-3); ucoord(40); ucoord(50); ucoord(44);
    out.push_back(1);
    u16(4);                    // this contour's point count
    coord(0); coord(0); coord(30); coord(0); coord(30); coord(40); coord(0); coord(40);
  }

  void u16(int v) { out.push_back((v >> 8) & 0xff); out.push_back(v & 0xff); }
  void coord(int v) { if(wide) u16(v); else out.push_back((uint8_t)(int8_t)v); }
  void ucoord(int v) { if(wide) u16(v); else out.push_back((uint8_t)v); }
};

// On success the parser hands the caller a block that font_t only points into,
// so it has to outlive every check against `f`. Collect them and free at the end
// of the suite: dropping them leaks, which only Linux CI reports - ASan's leak
// detector is unavailable on Darwin.
static std::vector<uint8_t *> parsed_blocks;

static font_status_t parse(const std::vector<uint8_t> &data, font_t *f) {
  uint8_t *buf = nullptr;
  size_t n = 0;
  font_status_t s = parse_vector_font(data.data(), data.size(), f, &buf, &n);
  if(buf) parsed_blocks.push_back(buf);
  return s;
}

static void free_parsed_blocks() {
  for(uint8_t *b : parsed_blocks) PV_FREE(b);
  parsed_blocks.clear();
}

void test_font() {
  printf("font: a narrow font round-trips\n");
  {
    af_builder_t b(false);
    font_t f;
    CHECK(parse(b.out, &f) == FONT_OK);
    CHECK(f.glyph_count == 1);
    CHECK(f.units_per_em == 128.0f);
    CHECK(f.wide_points == false);
    CHECK(f.glyphs[0].codepoint == 'A');
    CHECK(f.glyphs[0].x == 2 && f.glyphs[0].y == -3);
    CHECK(f.glyphs[0].w == 40 && f.glyphs[0].h == 50);
    CHECK(f.glyphs[0].advance == 44);
    CHECK(f.glyphs[0].path_count == 1);
    CHECK(f.glyphs[0].paths[0].point_count == 4);
    glyph_path_point_t *p = (glyph_path_point_t *)f.glyphs[0].paths[0].points;
    CHECK(p[2].x == 30 && p[2].y == 40);
  }

  printf("font: a wide font keeps its em and 16-bit points\n");
  {
    af_builder_t b(true, 1024);
    font_t f;
    CHECK(parse(b.out, &f) == FONT_OK);
    CHECK(f.units_per_em == 1024.0f);
    CHECK(f.wide_points == true);
    glyph_path_point16_t *p = (glyph_path_point16_t *)f.glyphs[0].paths[0].points;
    CHECK(p[2].x == 30 && p[2].y == 40);
  }

  printf("font: an advance over 127 survives (it used to read negative)\n");
  {
    af_builder_t b(false);
    b.out[4 + 8 + 2 + 4] = 254;   // the advance byte
    font_t f;
    CHECK(parse(b.out, &f) == FONT_OK);
    CHECK(f.glyphs[0].advance == 254);
  }

  printf("font: bad input is reported, not guessed at\n");
  {
    font_t f;
    std::vector<uint8_t> empty;
    CHECK(parse(empty, &f) == FONT_BAD_MAGIC);

    // A null buffer reads short rather than reaching memcpy. Only Linux CI
    // reports the difference; Darwin's memcpy carries no nonnull attribute.
    uint8_t *buf = nullptr;
    size_t n = 0;
    CHECK(parse_vector_font(nullptr, 0, &f, &buf, &n) == FONT_BAD_MAGIC);

    std::vector<uint8_t> notafont = {'P', 'N', 'G', 1, 2, 3, 4, 5};
    CHECK(parse(notafont, &f) == FONT_BAD_MAGIC);

    std::vector<uint8_t> magic = {'a', 'f', '!', '?'};
    CHECK(parse(magic, &f) == FONT_TRUNCATED);

    af_builder_t b(false);
    std::vector<uint8_t> cut(b.out.begin(), b.out.end() - 3);
    CHECK(parse(cut, &f) == FONT_TRUNCATED);

    af_builder_t future(false);
    future.out[5] = 0x80;                 // a flags bit no build knows
    CHECK(parse(future.out, &f) == FONT_UNSUPPORTED_FLAGS);

    af_builder_t liar(false);
    liar.out[9] = 0;                      // header claims no contours
    CHECK(parse(liar.out, &f) == FONT_BAD_HEADER);

    af_builder_t zero_em(true, 0);        // wide, but a zero em would divide by zero
    CHECK(parse(zero_em.out, &f) == FONT_BAD_HEADER);
  }

  free_parsed_blocks();
}
