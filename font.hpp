#pragma once

#include <stdint.h>
#include <string>

#include "brush.hpp"
#include "shape.hpp"
#include "types.hpp"
#include "mat3.hpp"

namespace picovector {

  class image_t;

  // Contour points come in two widths. A narrow font packs the whole em into a
  // signed byte, which is plenty for body text and visibly stepped once a glyph
  // is drawn at a large size; a wide font gets a finer grid at twice the point
  // storage. font_t::wide_points says which a given font holds.
  class glyph_path_point_t {
  public:
    int8_t x, y;

    vec2_t transform(mat3_t *transform);
  };

  class glyph_path_point16_t {
  public:
    int16_t x, y;

    vec2_t transform(mat3_t *transform);
  };

  class glyph_path_t {
  public:
    uint16_t point_count;
    void *points; // glyph_path_point_t[] or glyph_path_point16_t[]
  };

  class glyph_t {
  public:
    uint16_t codepoint;
    int16_t x, y, w, h;
    int16_t advance;
    uint8_t path_count;
    glyph_path_t *paths;

    rect_t bounds(mat3_t *transform);
  };

  class font_t {
  public:
    int glyph_count;
    glyph_t *glyphs;
    // Glyph units per em. Sizes are expressed in the same terms, so this is what
    // a point size is scaled against.
    float units_per_em = 128.0f;
    bool wide_points = false;

    void draw(image_t *target, const char *text, float size);
    rect_t measure(image_t *target, const char *text, float size);

    // Length-bounded variants for drawing/measuring a substring (e.g. one
    // word span) without a NUL terminator, so the text layout path stays
    // zero-copy over the source buffer. `end` is one past the last byte.
    void draw(image_t *target, const char *text, const char *end, float size);
    rect_t measure(image_t *target, const char *text, const char *end, float size);
  };

  // ── .af parsing ─────────────────────────────────────────────────────────────
  // A byte source for the parser: a read function plus whatever handle it needs.
  // Matches the callback shape the vendored PNG/JPEG decoders take, so an
  // embedder can hand over a file object, a stream or a memory blob without the
  // parser knowing anything about it. A read returning fewer than `len` bytes is
  // a truncated file.
  struct font_reader_t {
    size_t (*read)(void *handle, void *dest, size_t len);
    void *handle;
  };

  enum font_status_t {
    FONT_OK = 0,
    FONT_BAD_MAGIC,         // not a .af at all - the caller may try another format
    FONT_TRUNCATED,         // ran out of bytes mid-parse
    FONT_UNSUPPORTED_FLAGS, // a flags bit this build doesn't know how to read
    FONT_BAD_HEADER,        // header read, but the values in it don't work
    FONT_MEM_ERROR
  };

  // Parse a .af into `font`, allocating one combined block for the glyph, path
  // and point tables. The block's internal pointers all stay within itself, so
  // it is allocated no-scan; it is returned through `buffer` for the caller to
  // keep alive (and free, where the allocator isn't a tracing GC). `font` is
  // only written on FONT_OK.
  font_status_t parse_vector_font(font_reader_t reader, font_t *font,
                                  uint8_t **buffer, size_t *buffer_size);
  font_status_t parse_vector_font(const uint8_t *data, size_t size, font_t *font,
                                  uint8_t **buffer, size_t *buffer_size);

}
