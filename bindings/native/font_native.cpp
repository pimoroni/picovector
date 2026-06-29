// native/font.cpp — hand-written bodies for font.load / font.__del__.
//
// These parse the binary .af font container and free its backing buffer:
// file I/O and manual heap layout that don't reduce to "call X, convert the
// result", so they're maintained here rather than generated. The DSL
// (api/font.py) still owns the type, registration and docstrings.

#include "pv_bindings.hpp"

extern "C" {
  #include "py/stream.h"
  #include "py/reader.h"
  #include "py/runtime.h"
  #include "extmod/vfs.h"

  mp_obj_t font__del__(mp_obj_t self_in) {
    self(self_in, font_obj_t);
#if MICROPY_MALLOC_USES_ALLOCATED_SIZE
    m_free(self->buffer, self->buffer_size);
#else
    m_free(self->buffer);
#endif
    return mp_const_none;
  }

  mp_obj_t font_load(size_t n_args, const mp_obj_t *args) {
#if PV_METRICS
    pv::metric_scope _pvm(PV_M_font_load);
#endif
    mp_obj_t path = args[0];
    font_obj_t *result = mp_obj_malloc_with_finaliser(font_obj_t, &type_font);

    mp_obj_t open_args[2] = { path, MP_ROM_QSTR(MP_QSTR_r) };
    mp_obj_t file = mp_vfs_open(MP_ARRAY_SIZE(open_args), open_args,
                               (mp_map_t *)&mp_const_empty_map);

    int error;
    char marker[4];
    mp_stream_read_exactly(file, &marker, sizeof(marker), &error);
    if (memcmp(marker, "af!?", 4) != 0) {
      mp_raise_msg_varg(&mp_type_OSError,
                        MP_ERROR_TEXT("failed to load font, missing AF header"));
    }

    uint16_t flags       = ru16(file);
    uint16_t glyph_count = ru16(file);
    uint16_t path_count  = ru16(file);
    uint16_t point_count = ru16(file);

    size_t glyph_buffer_size = sizeof(glyph_t) * glyph_count;
    size_t path_buffer_size  = sizeof(glyph_path_t) * path_count;
    size_t point_buffer_size = sizeof(glyph_path_point_t) * point_count;

    result->buffer_size = glyph_buffer_size + path_buffer_size + point_buffer_size;
    // one self-contained block: its internal glyph->paths / path->points
    // pointers stay within itself, and the whole block is kept alive by the
    // scanned font_obj, so it needs no GC scanning.
    result->buffer = (uint8_t *)m_malloc_no_scan(result->buffer_size);
    if (!result->buffer) {
      mp_raise_msg_varg(&mp_type_OSError,
                        MP_ERROR_TEXT("couldn't allocate buffer for font data"));
    }

    glyph_t *glyphs = (glyph_t *)result->buffer;
    glyph_path_t *paths = (glyph_path_t *)(result->buffer + glyph_buffer_size);
    glyph_path_point_t *points =
        (glyph_path_point_t *)(result->buffer + glyph_buffer_size + path_buffer_size);

    result->font.glyph_count = glyph_count;
    result->font.glyphs = glyphs;
    for (int i = 0; i < glyph_count; i++) {
      glyph_t *glyph = &result->font.glyphs[i];
      glyph->codepoint  = ru16(file);
      glyph->x          = rs8(file);
      glyph->y          = rs8(file);
      glyph->w          = ru8(file);
      glyph->h          = ru8(file);
      glyph->advance    = ru8(file);
      glyph->path_count = ru8(file);
      glyph->paths      = paths;
      paths += glyph->path_count;
    }
    for (int i = 0; i < glyph_count; i++) {
      glyph_t *glyph = &result->font.glyphs[i];
      for (int j = 0; j < glyph->path_count; j++) {
        glyph_path_t *path = &glyph->paths[j];
        path->point_count = flags & 0b1 ? ru16(file) : ru8(file);
        path->points = points;
        points += path->point_count;
      }
    }
    for (int i = 0; i < glyph_count; i++) {
      glyph_t *glyph = &result->font.glyphs[i];
      for (int j = 0; j < glyph->path_count; j++) {
        glyph_path_t *path = &glyph->paths[j];
        for (int k = 0; k < path->point_count; k++) {
          glyph_path_point_t *point = &path->points[k];
          point->x = ru8(file);
          point->y = ru8(file);
        }
      }
    }

    mp_stream_close(file);
    return MP_OBJ_FROM_PTR(result);
  }

}
