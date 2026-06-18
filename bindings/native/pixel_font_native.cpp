// native/pixel_font.cpp — hand-written bodies for pixel_font.load / __del__.
// See native/font.cpp for why these live here rather than being generated.

#include "pv_bindings.hpp"

extern "C" {
  #include <inttypes.h>
  #include "py/stream.h"
  #include "py/reader.h"
  #include "py/runtime.h"
  #include "extmod/vfs.h"

  mp_obj_t pixel_font__del__(mp_obj_t self_in) {
    self(self_in, pixel_font_obj_t);
#if MICROPY_MALLOC_USES_ALLOCATED_SIZE
    m_free(self->glyph_buffer, self->glyph_buffer_size);
    m_free(self->glyph_data_buffer, self->glyph_data_buffer_size);
#else
    m_free(self->glyph_buffer);
    m_free(self->glyph_data_buffer);
#endif
    return mp_const_none;
  }

  mp_obj_t pixel_font_load(size_t n_args, const mp_obj_t *args) {
    mp_obj_t path = args[0];
    pixel_font_obj_t *result =
        mp_obj_malloc_with_finaliser(pixel_font_obj_t, &type_pixel_font);

    mp_obj_t open_args[2] = { path, MP_ROM_QSTR(MP_QSTR_r) };
    mp_obj_t file = mp_vfs_open(MP_ARRAY_SIZE(open_args), open_args,
                               (mp_map_t *)&mp_const_empty_map);

    int error;
    char marker[4];
    mp_stream_read_exactly(file, &marker, sizeof(marker), &error);
    if (memcmp(marker, "ppf!", 4) != 0) {
      mp_raise_msg_varg(&mp_type_OSError,
                        MP_ERROR_TEXT("failed to load font, missing PPF header"));
    }

    uint16_t flags        = ru16(file);
    uint32_t glyph_count  = ru32(file);
    uint16_t glyph_width  = ru16(file);
    uint16_t glyph_height = ru16(file);
    (void)flags;

    char name[32];
    mp_stream_read_exactly(file, name, sizeof(name), &error);

    uint32_t bpr = floor((glyph_width + 7) / 8);
    uint32_t glyph_data_size = bpr * glyph_height;

    result->glyph_buffer_size = sizeof(pixel_font_glyph_t) * glyph_count;
    result->glyph_buffer = (uint8_t *)m_malloc(result->glyph_buffer_size);
    result->glyph_data_buffer_size = glyph_data_size * glyph_count;
    result->glyph_data_buffer = (uint8_t *)m_malloc(result->glyph_data_buffer_size);
    if (!result->glyph_buffer || !result->glyph_data_buffer) {
      mp_raise_msg_varg(&mp_type_OSError,
                        MP_ERROR_TEXT("couldn't allocate buffer for font data"));
    }

    pixel_font_glyph_t *glyphs = (pixel_font_glyph_t *)result->glyph_buffer;
    for (uint32_t i = 0; i < glyph_count; i++) {
      glyphs[i].codepoint = ru32(file);
      glyphs[i].width = ru16(file);
    }
    mp_stream_read_exactly(file, result->glyph_data_buffer,
                           result->glyph_data_buffer_size, &error);

    result->font = m_new_class(pixel_font_t);
    result->font->glyph_count     = glyph_count;
    result->font->width           = glyph_width;
    result->font->height          = glyph_height;
    result->font->glyph_data_size = glyph_data_size;
    result->font->glyphs          = glyphs;
    result->font->glyph_data      = result->glyph_data_buffer;
    strcpy(result->font->name, name);

    mp_stream_close(file);
    return MP_OBJ_FROM_PTR(result);
  }

}
