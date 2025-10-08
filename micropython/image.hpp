#include <algorithm>
#include "mp_tracked_allocator.hpp"
#include "../picovector.hpp"
#include "../image.hpp"
#include "../span.hpp"
#include "../font.hpp"


#define self(self_in, T) T *self = (T *)MP_OBJ_TO_PTR(self_in)
#define m_new_class(cls, ...) new(m_new(cls, 1)) cls(__VA_ARGS__)
#define m_del_class(cls, ptr) ptr->~cls(); m_del(cls, ptr, 1)

using namespace picovector;
using namespace std;

extern "C" {

  #include "py/stream.h"
  #include "py/reader.h"
  #include "py/runtime.h"
  #include "extmod/vfs.h"

  #ifndef NO_QSTR
    #include "PNGdec.h"
  #endif  

  extern const mp_obj_type_t type_Image;

  typedef struct _image_obj_t {
    mp_obj_base_t base;
    image *image;
  } image_obj_t;

  mp_obj_t image__del__(mp_obj_t self_in) {
    self(self_in, image_obj_t);
    if(self->image) {
      m_del_class(image, self->image);
    }
    return mp_const_none;
  }

    
  void *pngdec_open_callback(const char *filename, int32_t *size) {
    mp_obj_t fn = mp_obj_new_str(filename, (mp_uint_t)strlen(filename));

    mp_obj_t args[2] = {
        fn,
        MP_ROM_QSTR(MP_QSTR_r),
    };

    // Stat the file to get its size
    // example tuple response: (32768, 0, 0, 0, 0, 0, 5153, 1654709815, 1654709815, 1654709815)
    mp_obj_t stat = mp_vfs_stat(fn);
    mp_obj_tuple_t *tuple = (mp_obj_tuple_t*)MP_OBJ_TO_PTR(stat);
    *size = mp_obj_get_int(tuple->items[6]);

    mp_obj_t fhandle = mp_vfs_open(MP_ARRAY_SIZE(args), &args[0], (mp_map_t *)&mp_const_empty_map);

    return (void *)fhandle;
  }

  void pngdec_close_callback(void *handle) {
    mp_stream_close((mp_obj_t)handle);
  }

  int32_t pngdec_read_callback(PNGFILE *png, uint8_t *p, int32_t c) {
    mp_obj_t fhandle = png->fHandle;
    int error;
    return mp_stream_read_exactly(fhandle, p, c, &error);
  }

  // Re-implementation of stream.c/static mp_obj_t stream_seek(size_t n_args, const mp_obj_t *args)
  int32_t pngdec_seek_callback(PNGFILE *png, int32_t p) {
    mp_obj_t fhandle = png->fHandle;
    struct mp_stream_seek_t seek_s;
    seek_s.offset = p;
    seek_s.whence = SEEK_SET;

    const mp_stream_p_t *stream_p = mp_get_stream(fhandle);

    int error;
    mp_uint_t res = stream_p->ioctl(fhandle, MP_STREAM_SEEK, (mp_uint_t)(uintptr_t)&seek_s, &error);
    if (res == MP_STREAM_ERROR) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("PNG: seek failed with %d"), error);
    }

    return seek_s.offset;
  }



  void pngdec_decode_callback(PNGDRAW *pDraw) {
    image *i = (image *)pDraw->pUser;

    uint32_t *pdst = i->ptr(0, pDraw->y);
    uint8_t *psrc = (uint8_t *)pDraw->pPixels;
    int c = pDraw->iWidth;

    switch(pDraw->iPixelType) {
      case PNG_PIXEL_TRUECOLOR: {
        while(c--) {
          *pdst = _make_col(psrc[0], psrc[1], psrc[2], 255);
          psrc += 3;
          pdst++;
        }
      } break;

      case PNG_PIXEL_TRUECOLOR_ALPHA: {
        while(c--) {
          *pdst = _make_col(psrc[0], psrc[1], psrc[2], psrc[3]);
          psrc += 4;
          pdst++;
        }
      } break;

      case PNG_PIXEL_INDEXED: {
        while(c--) {
          uint8_t pi = *psrc;
          // do something with index here

          *pdst = _make_col(
            pDraw->pPalette[(pi * 3) + 0],
            pDraw->pPalette[(pi * 3) + 1],
            pDraw->pPalette[(pi * 3) + 2],
            pDraw->iHasAlpha ? pDraw->pPalette[768 + pi] : 255
          );

          psrc++;
          pdst++;
        }
      } break;

      case PNG_PIXEL_GRAYSCALE: {
        while(c--) {
          uint8_t src = *psrc;
          // do something with index here

          switch(pDraw->iBpp) {
            case 8: {
              *pdst = _make_col(src, src, src);
              pdst++;
            } break;

            case 4: {
              int src1 = (src & 0xf0) | ((src & 0xf0) >> 4);
              int src2 = (src & 0x0f) | ((src & 0x0f) << 4);
              *pdst = _make_col(src1, src1, src1);
              pdst++;
              *pdst = _make_col(src2, src2, src2);
              pdst++;
            } break;

            case 1: {
              for(int i = 0; i < 8; i++) {
                int v = src & 0b10000000 ? 255 : 0;
                *pdst = _make_col(v, v, v);
                pdst++;
                src <<= 1;
              }              
            } break;
          }

          psrc++;          
        }
      } break;

      default: {
        // TODO: raise file not supported error
      } break;
    }

//     } else if (pDraw->iPixelType == PNG_PIXEL_INDEXED) {
//         for(int x = 0; x < pDraw->iWidth; x++) {
//             uint8_t i = 0;
//             if(pDraw->iBpp == 8) {  // 8bpp
//                 i = *pixel++;
//             } else if (pDraw->iBpp == 4) {  // 4bpp
//                 i = *pixel;
//                 i >>= (x & 0b1) ? 0 : 4;
//                 i &= 0xf;
//                 if (x & 1) pixel++;
//             } else if (pDraw->iBpp == 2) {  // 2bpp
//                 i = *pixel;
//                 i >>= 6 - ((x & 0b11) << 1);
//                 i &= 0x3;
//                 if ((x & 0b11) == 0b11) pixel++;
//             } else {  // 1bpp
//                 i = *pixel;
//                 i >>= 7 - (x & 0b111);
//                 i &= 0b1;
//                 if ((x & 0b111) == 0b111) pixel++;
//             }
//             if(x < target->source.x || x >= target->source.x + target->source.w) continue;
//             // grab the colour from the palette
//             uint8_t r = pDraw->pPalette[(i * 3) + 0];
//             uint8_t g = pDraw->pPalette[(i * 3) + 1];
//             uint8_t b = pDraw->pPalette[(i * 3) + 2];
//             uint8_t a = pDraw->iHasAlpha ? pDraw->pPalette[768 + i] : 1;
//             if (a) {
//                 if (current_graphics->pen_type == PicoGraphics::PEN_RGB332) {
//                     if (current_mode == MODE_POSTERIZE || current_mode == MODE_COPY) {
//                         // Posterized output to RGB332
//                         current_graphics->set_pen(RGB(r, g, b).to_rgb332());
//                         current_graphics->rectangle({current_position.x, current_position.y, scale.x, scale.y});
//                     } else {
//                         // Dithered output to RGB332
//                         for(auto px = 0; px < scale.x; px++) {
//                             for(auto py = 0; py < scale.y; py++) {
//                                 current_graphics->set_pixel_dither(current_position + Point{px, py}, {r, g, b});
//                             }
//                         }
//                     }
//                 } else if(current_graphics->pen_type == PicoGraphics::PEN_P8
//                     || current_graphics->pen_type == PicoGraphics::PEN_P4
//                     || current_graphics->pen_type == PicoGraphics::PEN_3BIT
//                     || current_graphics->pen_type == PicoGraphics::PEN_INKY7) {

//                         // Copy raw palette indexes over
//                         if(current_mode == MODE_COPY) {
//                             if(current_palette_offset > 0) {
//                                 i = ((int16_t)(i) + current_palette_offset) & 0xff;
//                             }
//                             current_graphics->set_pen(i);
//                             current_graphics->rectangle({current_position.x, current_position.y, scale.x, scale.y});
//                         // Posterized output to the available palete
//                         } else if(current_mode == MODE_POSTERIZE) {
//                             int closest = RGB(r, g, b).closest(current_graphics->get_palette(), current_graphics->get_palette_size());
//                             if (closest == -1) {
//                                 closest = 0;
//                             }
//                             current_graphics->set_pen(closest);
//                             current_graphics->rectangle({current_position.x, current_position.y, scale.x, scale.y});
//                         } else {
//                             for(auto px = 0; px < scale.x; px++) {
//                                 for(auto py = 0; py < scale.y; py++) {
//                                     current_graphics->set_pixel_dither(current_position + Point{px, py}, {r, g, b});
//                                 }
//                             }
//                         }
//                 } else {
//                     current_graphics->set_pen(r, g, b);
//                     current_graphics->rectangle({current_position.x, current_position.y, scale.x, scale.y});
//                 }
//             }
//             current_position += step;
//         }
//     }

    // samples of other pixel formats at end of file...
  }

  static mp_obj_t image_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    image_obj_t *self = m_new_obj(image_obj_t);
    self->base.type = type;

    int w = mp_obj_get_int(args[0]);    
    int h = mp_obj_get_int(args[1]);    

    self->image = new(m_malloc(sizeof(image))) image(w, h);

    return MP_OBJ_FROM_PTR(self);
  }

  mp_obj_t image_load(mp_obj_t path) {
    const char *s = mp_obj_str_get_str(path);    
    image_obj_t *result = mp_obj_malloc_with_finaliser(image_obj_t, &type_Image);

    PNG *png = new(m_malloc(sizeof(PNG))) PNG();
    int status = png->open(mp_obj_str_get_str(path), pngdec_open_callback, pngdec_close_callback, pngdec_read_callback, pngdec_seek_callback, pngdec_decode_callback);
    result->image = new(m_malloc(sizeof(image))) image(png->getWidth(), png->getHeight());
    png->decode((void *)result->image, 0);
#if PICO
    m_free(png);
#else
    m_free(png, sizeof(png));
#endif
    return MP_OBJ_FROM_PTR(result);
  }

  mp_obj_t image_window(size_t n_args, const mp_obj_t *pos_args) {
    const image_obj_t *self = (image_obj_t *)MP_OBJ_TO_PTR(pos_args[0]);
    int x = mp_obj_get_int(pos_args[1]);
    int y = mp_obj_get_int(pos_args[2]);
    int w = mp_obj_get_int(pos_args[3]);
    int h = mp_obj_get_int(pos_args[4]);
    image_obj_t *result = mp_obj_malloc_with_finaliser(image_obj_t, &type_Image);
    rect i = self->image->bounds.intersection(rect(x, y, w, h));
    result->image = new(m_malloc(sizeof(image))) image(self->image->ptr(i.x, i.y), i.w, i.h);
    result->image->_rowstride = self->image->_rowstride;
    return MP_OBJ_FROM_PTR(result);
  }


  mp_obj_t image_draw(size_t n_args, const mp_obj_t *pos_args) {
    const image_obj_t *self = (image_obj_t *)MP_OBJ_TO_PTR(pos_args[0]);
    const shape_obj_t *shape = (shape_obj_t *)MP_OBJ_TO_PTR(pos_args[1]);
    self->image->draw(shape->shape);
    return mp_const_none;
  }


  mp_obj_t image_text(size_t n_args, const mp_obj_t *pos_args) {
    const image_obj_t *self = (image_obj_t *)MP_OBJ_TO_PTR(pos_args[0]);
    const char *text = mp_obj_str_get_str(pos_args[1]);    
    
    float x = mp_obj_get_float(pos_args[2]);
    float y = mp_obj_get_float(pos_args[3]);
    float size = mp_obj_get_float(pos_args[4]);

    self->image->font->draw(self->image, text, x, y, size);

    return mp_const_none;
  }

  mp_obj_t image_measure_text(size_t n_args, const mp_obj_t *pos_args) {
    const image_obj_t *self = (image_obj_t *)MP_OBJ_TO_PTR(pos_args[0]);
    const char *text = mp_obj_str_get_str(pos_args[1]);    
    
    float size = mp_obj_get_float(pos_args[2]);

    rect r = self->image->font->measure(self->image, text, size);

    mp_obj_t result[2];
    result[0] = mp_obj_new_float(r.w);
    result[1] = mp_obj_new_float(size);
    return mp_obj_new_tuple(2, result);
  }


  mp_obj_t image_blit(size_t n_args, const mp_obj_t *pos_args) {
    const image_obj_t *self = (image_obj_t *)MP_OBJ_TO_PTR(pos_args[0]);
    const image_obj_t *src = (image_obj_t *)MP_OBJ_TO_PTR(pos_args[1]);
    int x = mp_obj_get_float(pos_args[2]);    
    int y = mp_obj_get_float(pos_args[3]);    

    src->image->blit(self->image, point(x, y));
    return mp_const_none;
  }

  mp_obj_t image_scale_blit(size_t n_args, const mp_obj_t *pos_args) {
    const image_obj_t *self = (image_obj_t *)MP_OBJ_TO_PTR(pos_args[0]);
    const image_obj_t *src = (image_obj_t *)MP_OBJ_TO_PTR(pos_args[1]);
    int x = mp_obj_get_float(pos_args[2]);    
    int y = mp_obj_get_float(pos_args[3]);    
    int w = mp_obj_get_float(pos_args[4]);    
    int h = mp_obj_get_float(pos_args[5]);    

    src->image->blit(self->image, rect(x, y, w, h));
    return mp_const_none;
  }

  mp_obj_t image_clear(mp_obj_t self_in) {
    self(self_in, image_obj_t);
    self->image->clear();
    return mp_const_none;
  }


  mp_obj_t image_font(size_t n_args, const mp_obj_t *pos_args) {
    const image_obj_t *self = (image_obj_t *)MP_OBJ_TO_PTR(pos_args[0]);
    font_obj_t *font = (font_obj_t *)MP_OBJ_TO_PTR(pos_args[1]);
    self->image->font = &font->font;
    return mp_const_none;
  }

  mp_obj_t image_brush(size_t n_args, const mp_obj_t *pos_args) {
    const image_obj_t *self = (image_obj_t *)MP_OBJ_TO_PTR(pos_args[0]);
    const brush_obj_t *brush = (brush_obj_t *)MP_OBJ_TO_PTR(pos_args[1]);
    self->image->brush = brush->brush;
    return mp_const_none;
  }


  mp_obj_t image_alpha(size_t n_args, const mp_obj_t *pos_args) {    
    const image_obj_t *self = (image_obj_t *)MP_OBJ_TO_PTR(pos_args[0]);    
    float a = mp_obj_get_float(pos_args[1]);    
    a = min(255.0f, max(a, 0.0f));
    self->image->alpha = int(a);
    return mp_const_none;
  }

  mp_obj_t image_antialias(size_t n_args, const mp_obj_t *pos_args) {    
    const image_obj_t *self = (image_obj_t *)MP_OBJ_TO_PTR(pos_args[0]);    
    int aa = mp_obj_get_int(pos_args[1]);    
    self->image->antialias = static_cast<antialiasing>(aa);
    return mp_const_none;
  }


  static void image_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    self(self_in, image_obj_t);


    if(attr == MP_QSTR_width && dest[0] == MP_OBJ_NULL) {
      dest[0] = mp_obj_new_int(self->image->bounds.w);
      return;
    }      

    if(attr == MP_QSTR_height && dest[0] == MP_OBJ_NULL) {
      dest[0] = mp_obj_new_int(self->image->bounds.h);
      return;
    }      

    dest[1] = MP_OBJ_SENTINEL;
  }

  /*
    micropython bindings
  */
  static MP_DEFINE_CONST_FUN_OBJ_1(image__del___obj, image__del__);
  
  static MP_DEFINE_CONST_FUN_OBJ_VAR(image_draw_obj, 2, image_draw);
  static MP_DEFINE_CONST_FUN_OBJ_VAR(image_window_obj, 5, image_window);
  
  static MP_DEFINE_CONST_FUN_OBJ_1(image_clear_obj, image_clear);  

  static MP_DEFINE_CONST_FUN_OBJ_VAR(image_brush_obj, 2, image_brush);
  static MP_DEFINE_CONST_FUN_OBJ_VAR(image_font_obj, 2, image_font);
  static MP_DEFINE_CONST_FUN_OBJ_VAR(image_text_obj, 5, image_text);
  static MP_DEFINE_CONST_FUN_OBJ_VAR(image_measure_text_obj, 3, image_measure_text);
  static MP_DEFINE_CONST_FUN_OBJ_VAR(image_alpha_obj, 2, image_alpha);
  static MP_DEFINE_CONST_FUN_OBJ_VAR(image_antialias_obj, 2, image_antialias);
  static MP_DEFINE_CONST_FUN_OBJ_VAR(image_blit_obj, 4, image_blit);
  static MP_DEFINE_CONST_FUN_OBJ_VAR(image_scale_blit_obj, 4, image_scale_blit);

  static MP_DEFINE_CONST_FUN_OBJ_1(image_load_obj, image_load);
  static MP_DEFINE_CONST_STATICMETHOD_OBJ(image_load_static_obj, MP_ROM_PTR(&image_load_obj));

  static const mp_rom_map_elem_t image_locals_dict_table[] = {
      { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&image__del___obj) },
      { MP_ROM_QSTR(MP_QSTR_draw), MP_ROM_PTR(&image_draw_obj) },
      { MP_ROM_QSTR(MP_QSTR_window), MP_ROM_PTR(&image_window_obj) },
      { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&image_clear_obj) },
      { MP_ROM_QSTR(MP_QSTR_brush), MP_ROM_PTR(&image_brush_obj) },
      { MP_ROM_QSTR(MP_QSTR_font), MP_ROM_PTR(&image_font_obj) },
      { MP_ROM_QSTR(MP_QSTR_text), MP_ROM_PTR(&image_text_obj) },
      { MP_ROM_QSTR(MP_QSTR_measure_text), MP_ROM_PTR(&image_measure_text_obj) },
      { MP_ROM_QSTR(MP_QSTR_alpha), MP_ROM_PTR(&image_alpha_obj) },
      { MP_ROM_QSTR(MP_QSTR_antialias), MP_ROM_PTR(&image_antialias_obj) },
      { MP_ROM_QSTR(MP_QSTR_blit), MP_ROM_PTR(&image_blit_obj) },
      { MP_ROM_QSTR(MP_QSTR_scale_blit), MP_ROM_PTR(&image_scale_blit_obj) },
      { MP_ROM_QSTR(MP_QSTR_load), MP_ROM_PTR(&image_load_static_obj) },
      
  };
  static MP_DEFINE_CONST_DICT(image_locals_dict, image_locals_dict_table);

  MP_DEFINE_CONST_OBJ_TYPE(
      type_Image,
      MP_QSTR_Image,
      MP_TYPE_FLAG_NONE,
      make_new, (const void *)image_new,
      attr, (const void *)image_attr,
      locals_dict, &image_locals_dict
  );

}












//     } else if (pDraw->iPixelType == PNG_PIXEL_GRAYSCALE) {
//         for(int x = 0; x < pDraw->iWidth; x++) {
//             uint8_t i = 0;
//             if(pDraw->iBpp == 8) {  // 8bpp
//                 i = *pixel++; // Already 8bpc
//             } else if (pDraw->iBpp == 4) {  // 4bpp
//                 i = *pixel;
//                 i >>= (x & 0b1) ? 0 : 4;
//                 i &= 0xf;
//                 if (x & 1) pixel++;
//                 // Just copy the colour into the upper and lower nibble
//                 if(current_mode != MODE_COPY) {
//                     i = (i << 4) | i;
//                 }
//             } else if (pDraw->iBpp == 2) {  // 2bpp
//                 i = *pixel;
//                 i >>= 6 - ((x & 0b11) << 1);
//                 i &= 0x3;
//                 if ((x & 0b11) == 0b11) pixel++;
//                 // Evenly spaced 4-colour palette
//                 if(current_mode != MODE_COPY) {
//                     i = (0xFFB86800 >> (i * 8)) & 0xFF;
//                 }
//             } else {  // 1bpp
//                 i = *pixel;
//                 i >>= 7 - (x & 0b111);
//                 i &= 0b1;
//                 if ((x & 0b111) == 0b111) pixel++;
//                 if(current_mode != MODE_COPY) {
//                     i = i ? 255 : 0;
//                 }
//             }
//             if(x < target->source.x || x >= target->source.x + target->source.w) continue;

//             //mp_printf(&mp_plat_print, "Drawing pixel at %dx%d, %dbpp, value %d\n", current_position.x, current_position.y, pDraw->iBpp, i);
//             if (current_mode != MODE_PEN) {
//                 // Allow greyscale PNGs to be copied just like an indexed PNG
//                 // since we might want to offset and recolour them.
//                 if(current_mode == MODE_COPY
//                     && (current_graphics->pen_type == PicoGraphics::PEN_P8
//                     || current_graphics->pen_type == PicoGraphics::PEN_P4
//                     || current_graphics->pen_type == PicoGraphics::PEN_3BIT
//                     || current_graphics->pen_type == PicoGraphics::PEN_INKY7)) {
//                     if(current_palette_offset > 0) {
//                         i = ((int16_t)(i) + current_palette_offset) & 0xff;
//                     }
//                     current_graphics->set_pen(i);
//                 } else {
//                     current_graphics->set_pen(i, i, i);
//                 }
//             }
//             if (current_mode != MODE_PEN || i == 0) {
//                 current_graphics->rectangle({current_position.x, current_position.y, scale.x, scale.y});
//             }

//             current_position += step;
//         }
//     } else if (pDraw->iPixelType == PNG_PIXEL_INDEXED) {
//         for(int x = 0; x < pDraw->iWidth; x++) {
//             uint8_t i = 0;
//             if(pDraw->iBpp == 8) {  // 8bpp
//                 i = *pixel++;
//             } else if (pDraw->iBpp == 4) {  // 4bpp
//                 i = *pixel;
//                 i >>= (x & 0b1) ? 0 : 4;
//                 i &= 0xf;
//                 if (x & 1) pixel++;
//             } else if (pDraw->iBpp == 2) {  // 2bpp
//                 i = *pixel;
//                 i >>= 6 - ((x & 0b11) << 1);
//                 i &= 0x3;
//                 if ((x & 0b11) == 0b11) pixel++;
//             } else {  // 1bpp
//                 i = *pixel;
//                 i >>= 7 - (x & 0b111);
//                 i &= 0b1;
//                 if ((x & 0b111) == 0b111) pixel++;
//             }
//             if(x < target->source.x || x >= target->source.x + target->source.w) continue;
//             // grab the colour from the palette
//             uint8_t r = pDraw->pPalette[(i * 3) + 0];
//             uint8_t g = pDraw->pPalette[(i * 3) + 1];
//             uint8_t b = pDraw->pPalette[(i * 3) + 2];
//             uint8_t a = pDraw->iHasAlpha ? pDraw->pPalette[768 + i] : 1;
//             if (a) {
//                 if (current_graphics->pen_type == PicoGraphics::PEN_RGB332) {
//                     if (current_mode == MODE_POSTERIZE || current_mode == MODE_COPY) {
//                         // Posterized output to RGB332
//                         current_graphics->set_pen(RGB(r, g, b).to_rgb332());
//                         current_graphics->rectangle({current_position.x, current_position.y, scale.x, scale.y});
//                     } else {
//                         // Dithered output to RGB332
//                         for(auto px = 0; px < scale.x; px++) {
//                             for(auto py = 0; py < scale.y; py++) {
//                                 current_graphics->set_pixel_dither(current_position + Point{px, py}, {r, g, b});
//                             }
//                         }
//                     }
//                 } else if(current_graphics->pen_type == PicoGraphics::PEN_P8
//                     || current_graphics->pen_type == PicoGraphics::PEN_P4
//                     || current_graphics->pen_type == PicoGraphics::PEN_3BIT
//                     || current_graphics->pen_type == PicoGraphics::PEN_INKY7) {

//                         // Copy raw palette indexes over
//                         if(current_mode == MODE_COPY) {
//                             if(current_palette_offset > 0) {
//                                 i = ((int16_t)(i) + current_palette_offset) & 0xff;
//                             }
//                             current_graphics->set_pen(i);
//                             current_graphics->rectangle({current_position.x, current_position.y, scale.x, scale.y});
//                         // Posterized output to the available palete
//                         } else if(current_mode == MODE_POSTERIZE) {
//                             int closest = RGB(r, g, b).closest(current_graphics->get_palette(), current_graphics->get_palette_size());
//                             if (closest == -1) {
//                                 closest = 0;
//                             }
//                             current_graphics->set_pen(closest);
//                             current_graphics->rectangle({current_position.x, current_position.y, scale.x, scale.y});
//                         } else {
//                             for(auto px = 0; px < scale.x; px++) {
//                                 for(auto py = 0; py < scale.y; py++) {
//                                     current_graphics->set_pixel_dither(current_position + Point{px, py}, {r, g, b});
//                                 }
//                             }
//                         }
//                 } else {
//                     current_graphics->set_pen(r, g, b);
//                     current_graphics->rectangle({current_position.x, current_position.y, scale.x, scale.y});
//                 }
//             }
//             current_position += step;
//         }
//     }
