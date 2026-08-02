// The .af container parser.
//
// Layout, after the four-byte "af!?" marker, everything big-endian:
//
//   u16 flags, u16 glyph_count, u16 path_count, u16 point_count
//   u16 units_per_em                            only when the wide flag is set
//   glyph[glyph_count]                          codepoint, bbox, advance, path count
//   contour lengths[path_count]                 u8, or u16 per the 16-bit-count flag
//   points[point_count]                         x, y pairs
//
// Glyph metrics and points are a byte wide by default and 16 bits wide with the
// wide flag, which is what a font drawn at a large point size needs: a narrow
// font's whole em is 128 units, so a glyph filling a 240px screen quantises to
// steps of nearly two pixels.

#include <string.h>

#include "font.hpp"
#include "picovector.hpp"

namespace picovector {

  static const uint16_t AF_FLAG_16BIT_POINT_COUNT = 0b01;
  static const uint16_t AF_FLAG_WIDE              = 0b10;
  static const uint16_t AF_FLAGS_KNOWN = AF_FLAG_16BIT_POINT_COUNT | AF_FLAG_WIDE;

  namespace {

    // Big-endian reads over a font_reader_t. A short read latches `truncated`
    // and every subsequent read yields zero, so the parse walks to the end
    // without wandering off the data and reports once, at the point it can.
    struct af_reader_t {
      font_reader_t source;
      bool truncated;

      void bytes(void *dest, size_t len) {
        if(!truncated && source.read(source.handle, dest, len) == len) return;
        truncated = true;
        memset(dest, 0, len);
      }

      uint16_t u16() { uint8_t b[2]; bytes(b, 2); return (uint16_t)((b[0] << 8) | b[1]); }
      uint8_t  u8()  { uint8_t b;    bytes(&b, 1); return b; }
      int16_t  s16() { return (int16_t)u16(); }
      int8_t   s8()  { return (int8_t)u8(); }
    };

    struct memory_source_t {
      const uint8_t *data;
      size_t size;
      size_t pos;
    };

    size_t memory_read(void *handle, void *dest, size_t len) {
      memory_source_t *source = (memory_source_t *)handle;
      size_t available = source->size - source->pos;
      if(len > available) len = available;
      // memcpy's pointers are nonnull under glibc, and an empty buffer's is null.
      if(len == 0) return 0;
      memcpy(dest, source->data + source->pos, len);
      source->pos += len;
      return len;
    }

  }

  font_status_t parse_vector_font(font_reader_t reader, font_t *font,
                                  uint8_t **buffer, size_t *buffer_size) {
    af_reader_t r = { reader, false };

    char marker[4];
    r.bytes(marker, sizeof(marker));
    if(r.truncated || memcmp(marker, "af!?", 4) != 0) return FONT_BAD_MAGIC;

    uint16_t flags       = r.u16();
    uint16_t glyph_count = r.u16();
    uint16_t path_count  = r.u16();
    uint16_t point_count = r.u16();
    if(flags & ~AF_FLAGS_KNOWN) return FONT_UNSUPPORTED_FLAGS;

    bool wide = flags & AF_FLAG_WIDE;
    float units_per_em = 128.0f;
    if(wide) {
      uint16_t units = r.u16();
      if(!r.truncated && units == 0) return FONT_BAD_HEADER; // would divide by zero
      units_per_em = (float)units;
    }
    if(r.truncated) return FONT_TRUNCATED;   // nothing allocated yet

    size_t glyph_buffer_size = sizeof(glyph_t) * glyph_count;
    size_t path_buffer_size  = sizeof(glyph_path_t) * path_count;
    size_t point_buffer_size = (wide ? sizeof(glyph_path_point16_t)
                                     : sizeof(glyph_path_point_t)) * point_count;

    // The point table is carved out after two tables of pointer-bearing structs,
    // so it inherits their alignment; nothing here needs padding between them.
    static_assert(sizeof(glyph_t) % alignof(glyph_path_t) == 0, "path table would be misaligned");
    static_assert(sizeof(glyph_path_t) % alignof(glyph_path_point16_t) == 0, "point table would be misaligned");

    size_t total = glyph_buffer_size + path_buffer_size + point_buffer_size;
    uint8_t *block = (uint8_t *)PV_MALLOC_NO_SCAN(total);
    if(!block) return FONT_MEM_ERROR;

    // The block is raw storage with no destructor behind it, and `font` is only
    // written on FONT_OK, so a rejection past this point has to hand it back or
    // it is gone. Under a tracing GC that is collectable; under the default
    // PV_MALLOC it is a hard leak, and a device retrying a corrupt font leaks
    // every attempt.
    auto reject = [&](font_status_t status) {
#if MICROPY_MALLOC_USES_ALLOCATED_SIZE
      PV_FREE(block, total);
#else
      PV_FREE(block);
#endif
      return status;
    };

    glyph_t *glyphs = (glyph_t *)block;
    glyph_path_t *paths = (glyph_path_t *)(block + glyph_buffer_size);
    uint8_t *points = block + glyph_buffer_size + path_buffer_size;

    // Each pass hands out slices of the tables the header sized, so the totals
    // the glyphs actually claim are checked before the pass that writes through
    // them. A file claiming more than it declared is rejected, not trusted.
    size_t claimed_paths = 0;
    for(int i = 0; i < glyph_count; i++) {
      glyph_t *glyph = &glyphs[i];
      glyph->codepoint = r.u16();
      // The bbox origin is signed; the extents and the advance are not.
      glyph->x       = wide ? r.s16() : r.s8();
      glyph->y       = wide ? r.s16() : r.s8();
      glyph->w       = wide ? (int16_t)r.u16() : (int16_t)r.u8();
      glyph->h       = wide ? (int16_t)r.u16() : (int16_t)r.u8();
      glyph->advance = wide ? (int16_t)r.u16() : (int16_t)r.u8();
      glyph->path_count = r.u8();
      glyph->paths = paths;
      paths += glyph->path_count;
      claimed_paths += glyph->path_count;
    }
    if(claimed_paths > path_count) return reject(FONT_BAD_HEADER);

    size_t point_size = wide ? sizeof(glyph_path_point16_t) : sizeof(glyph_path_point_t);
    size_t claimed_points = 0;
    uint8_t *point = points;
    for(int i = 0; i < glyph_count; i++) {
      glyph_t *glyph = &glyphs[i];
      for(int j = 0; j < glyph->path_count; j++) {
        glyph_path_t *path = &glyph->paths[j];
        path->point_count = (flags & AF_FLAG_16BIT_POINT_COUNT) ? r.u16() : r.u8();
        path->points = point;
        point += path->point_count * point_size;
        claimed_points += path->point_count;
      }
    }
    if(claimed_points > point_count) return reject(FONT_BAD_HEADER);

    for(int i = 0; i < glyph_count; i++) {
      glyph_t *glyph = &glyphs[i];
      for(int j = 0; j < glyph->path_count; j++) {
        glyph_path_t *path = &glyph->paths[j];
        for(int k = 0; k < path->point_count; k++) {
          if(wide) {
            glyph_path_point16_t *p = &((glyph_path_point16_t *)path->points)[k];
            p->x = r.s16();
            p->y = r.s16();
          } else {
            glyph_path_point_t *p = &((glyph_path_point_t *)path->points)[k];
            p->x = r.s8();
            p->y = r.s8();
          }
        }
      }
    }

    if(r.truncated) return reject(FONT_TRUNCATED);

    font->glyph_count = glyph_count;
    font->glyphs = glyphs;
    font->units_per_em = units_per_em;
    font->wide_points = wide;
    *buffer = block;
    *buffer_size = total;
    return FONT_OK;
  }

  font_status_t parse_vector_font(const uint8_t *data, size_t size, font_t *font,
                                  uint8_t **buffer, size_t *buffer_size) {
    memory_source_t source = { data, size, 0 };
    font_reader_t reader = { memory_read, &source };
    return parse_vector_font(reader, font, buffer, buffer_size);
  }

}
