#include "mp_tracked_allocator.hpp"
#include "../picovector.hpp"
#include "../font.hpp"
#include "../span.hpp"

#define self(self_in, T) T *self = (T *)MP_OBJ_TO_PTR(self_in)
#define m_new_class(cls, ...) new(m_new(cls, 1)) cls(__VA_ARGS__)
#define m_del_class(cls, ptr) ptr->~cls(); m_del(cls, ptr, 1)

using namespace picovector;

extern "C" {

  #include "py/stream.h"
  #include "py/reader.h"
  #include "py/runtime.h"
  #include "extmod/vfs.h"

  extern const mp_obj_type_t type_Font;

  typedef struct _font_obj_t {
    mp_obj_base_t base;
    font font;
    uint8_t *buffer;
    uint32_t buffer_size;
  } font_obj_t;

  mp_obj_t font__del__(mp_obj_t self_in) {
    self(self_in, font_obj_t);
#if PICO
    m_free(self->buffer);
#else
    m_free(self->buffer, self->buffer_size);
#endif
    return mp_const_none;
  }

  // file reading helpers
  uint16_t ru16(mp_obj_t file) {
    int error;
    uint16_t result;
    mp_stream_read_exactly(file, &result, 2, &error);
    return __builtin_bswap16(result);
  }

  uint8_t ru8(mp_obj_t file) {
    int error;
    uint8_t result;
    mp_stream_read_exactly(file, &result, 1, &error);
    return result;
  }

  int8_t rs8(mp_obj_t file) {
    int error;
    int8_t result;
    mp_stream_read_exactly(file, &result, 1, &error);
    return result;
  }

  mp_obj_t font_load(mp_obj_t path) {
    //const char *s = mp_obj_str_get_str(path);    
    font_obj_t *result = mp_obj_malloc_with_finaliser(font_obj_t, &type_Font);

    // PNG *png = new(m_malloc(sizeof(PNG))) PNG();
    // int status = png->open(mp_obj_str_get_str(path), pngdec_open_callback, pngdec_close_callback, pngdec_read_callback, pngdec_seek_callback, pngdec_decode_callback);
    // result->image = new(m_malloc(sizeof(image))) image(png->getWidth(), png->getHeight());
    // png->decode((void *)result->image, 0);
    // m_free(png, sizeof(png));
    //mp_obj_t fn = mp_obj_new_str(s, (mp_uint_t)strlen(s));

    // get file size (maybe not needed?)
    // mp_obj_t stat = mp_vfs_stat(path);
    // mp_obj_tuple_t *tuple = (mp_obj_tuple_t*)MP_OBJ_TO_PTR(stat);
    // mp_int_t size = mp_obj_get_int(tuple->items[6]);

    // open the file for binary reading
    mp_obj_t args[2] = {path, MP_ROM_QSTR(MP_QSTR_r)};
    mp_obj_t file = mp_vfs_open(MP_ARRAY_SIZE(args), args, (mp_map_t *)&mp_const_empty_map);
    
    int error;

    char marker[4];
    mp_stream_read_exactly(file, &marker, sizeof(marker), &error);

    if(memcmp(marker, "af!?", 4) != 0) {   
      mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("failed to load font, missing AF header"));
    }
    
    uint16_t flags       = ru16(file);
    uint16_t glyph_count = ru16(file);
    uint16_t path_count  = ru16(file);
    uint16_t point_count = ru16(file);

    size_t glyph_buffer_size = sizeof(glyph) * glyph_count;
    size_t path_buffer_size = sizeof(glyph_path) * path_count;
    size_t point_buffer_size = sizeof(glyph_path_point) * point_count;

    // allocate buffer to store font glyph, path, and point data
    result->buffer_size = glyph_buffer_size + path_buffer_size + point_buffer_size;
    result->buffer = (uint8_t *)m_malloc(result->buffer_size);

    if(!result->buffer) {
      mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("couldn't allocate buffer for font data"));
    }    

    glyph *glyphs = (glyph *)result->buffer;
    glyph_path *paths = (glyph_path *)(result->buffer + glyph_buffer_size);
    glyph_path_point *points = (glyph_path_point *)(result->buffer + glyph_buffer_size + path_buffer_size);

    // load glyph dictionary
    result->font.glyph_count = glyph_count;
    result->font.glyphs      = glyphs;
    for(int i = 0; i < glyph_count; i++) {
      glyph *glyph = &result->font.glyphs[i];
      glyph->codepoint  = ru16(file);
      glyph->x          =  rs8(file);
      glyph->y          =  rs8(file);
      glyph->w          =  ru8(file);
      glyph->h          =  ru8(file);
      glyph->advance    =  ru8(file);
      glyph->path_count =  ru8(file);
      glyph->paths      =      paths;
      paths += glyph->path_count;
    }

    // load the glyph paths
    for(int i = 0; i < glyph_count; i++) {
      glyph *glyph = &result->font.glyphs[i];
      for(int j = 0; j < glyph->path_count; j++) {
        glyph_path *path = &glyph->paths[j];
        path->point_count = flags & 0b1 ? ru16(file) : ru8(file);                
        path->points = points;
        points += path->point_count;
      }
    }

    // load the glyph points
    for(int i = 0; i < glyph_count; i++) {
      glyph *glyph = &result->font.glyphs[i];
      for(int j = 0; j < glyph->path_count; j++) {
        glyph_path *path = &glyph->paths[j];
        for(int k = 0; k < path->point_count; k++) {
          glyph_path_point *point = &path->points[k];
          point->x = ru8(file);
          point->y = ru8(file);
        }
      }
    }
    
    mp_stream_close(file);

    return MP_OBJ_FROM_PTR(result);
  }

  static MP_DEFINE_CONST_FUN_OBJ_1(font__del___obj, font__del__);

  static MP_DEFINE_CONST_FUN_OBJ_1(font_load_obj, font_load);
  static MP_DEFINE_CONST_STATICMETHOD_OBJ(font_load_static_obj, MP_ROM_PTR(&font_load_obj));

  static const mp_rom_map_elem_t font_locals_dict_table[] = {
      { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&font__del___obj) },
      { MP_ROM_QSTR(MP_QSTR_load), MP_ROM_PTR(&font_load_static_obj) },
      
  };
  static MP_DEFINE_CONST_DICT(font_locals_dict, font_locals_dict_table);

  MP_DEFINE_CONST_OBJ_TYPE(
      type_Font,
      MP_QSTR_Font,
      MP_TYPE_FLAG_NONE,
      locals_dict, &font_locals_dict
  );

}