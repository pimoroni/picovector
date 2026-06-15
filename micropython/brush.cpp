#include "mp_helpers.hpp"
#include "picovector.hpp"

extern "C" {
  #include "py/runtime.h"

  /*MPY_BIND_DEL(brush, {
    self(self_in, brush_obj_t);
    m_del_class(brush_t, self->brush);
    return mp_const_none;
  })*/


  // MPY_BIND_STATICMETHOD_VAR(3, xor, {
  //   int r = mp_obj_get_float(args[0]);
  //   int g = mp_obj_get_float(args[1]);
  //   int b = mp_obj_get_float(args[2]);
  //   brush_obj_t *brush = mp_obj_malloc(brush_obj_t, &type_brush);
  //   //brush->brush = m_new_class(xor_brush, _make_col(r, g, b));
  //   return MP_OBJ_FROM_PTR(brush);
  // })


  // MPY_BIND_STATICMETHOD_VAR(1, brighten, {
  //   int amount = mp_obj_get_float(args[0]);
  //   brush_obj_t *brush = mp_obj_malloc(brush_obj_t, &type_brush);
  //   //brush->brush = m_new_class(brighten_brush, amount);
  //   return MP_OBJ_FROM_PTR(brush);
  // })


  MPY_BIND_STATICMETHOD_VAR(3, pattern, {
    if(!mp_obj_is_type(args[0], &type_color) ||
       !mp_obj_is_type(args[1], &type_color)) {
      mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("invalid parameter, expected brush.pattern(color, color, index | tuple[8], [on=image])"));
    }

    const color_obj_t *c1 = (color_obj_t *)MP_OBJ_TO_PTR(args[0]);
    const color_obj_t *c2 = (color_obj_t *)MP_OBJ_TO_PTR(args[1]);

    brush_obj_t *brush = mp_obj_malloc(brush_obj_t, &type_brush);
    if(mp_obj_is_int(args[2])) { // brush index supplied, use pre-baked brush
      int pattern_index = mp_obj_get_int(args[2]);
      if(pattern_index < 0 || pattern_index > 37) {
        mp_raise_TypeError(MP_ERROR_TEXT("invalid parameter, pattern index must be a number between 0 and 37"));
      }
      brush->brush = m_new_class(pattern_brush_t, *c1->c, *c2->c, pattern_index);
    }else if(mp_obj_is_type(args[2], &mp_type_tuple)) { // custom pattern passed as tuple
      size_t len;
      mp_obj_t *items;
      mp_obj_get_array(args[2], &len, &items);

      if(len != 8) {
        mp_raise_TypeError(MP_ERROR_TEXT("invalid parameter, custom pattern must be a tuple with 8 elements"));
      }
      uint8_t pattern[8];
      for(int i = 0; i < 8; i++) {
        pattern[i] = mp_obj_get_int(items[i]);
      }
      brush->brush = m_new_class(pattern_brush_t, *c1->c, *c2->c, pattern);
    } else {
      mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("invalid parameter, expected brush.pattern(color, color, index | tuple[8], [on=image])"));
    }

    return MP_OBJ_FROM_PTR(brush);
  })


  MPY_BIND_STATICMETHOD_VAR(1, image, {
    if(!mp_obj_is_type(args[0], &type_image) ||
       (n_args >= 1 && !mp_obj_is_type(args[1], &type_mat3))) {
      mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("invalid parameter, expected brush.image(image, [mat3], [on=image])"));
    }
    brush_obj_t *brush = mp_obj_malloc(brush_obj_t, &type_brush);
    const image_obj_t *src = (image_obj_t *)MP_OBJ_TO_PTR(args[0]);

    if(n_args == 1) {
      brush->brush = m_new_class(image_brush_t, src->image);
    } else {
      mat3_obj_t *transform = (mat3_obj_t *)MP_OBJ_TO_PTR(args[1]);
      mat3_t *m = &transform->m;
      brush->brush = m_new_class(image_brush_t, src->image, m);
    }

    return MP_OBJ_FROM_PTR(brush);
  })


  MPY_BIND_STATICMETHOD_VAR(6, gradient, {
    // gradient(type, x1, y1, x2, y2, stops, [transform])
    //   type:   brush.LINEAR or brush.RADIAL
    //   x1..y2: gradient geometry in the gradient's coordinate space (0..1 for SVG)
    //   stops:  sequence of (position, color), position 0..1, up to 16 stops
    //   transform: optional mat3 mapping gradient space onto device pixels
    int gtype = mp_obj_get_int(args[0]);
    float x1 = mp_obj_get_float(args[1]);
    float y1 = mp_obj_get_float(args[2]);
    float x2 = mp_obj_get_float(args[3]);
    float y2 = mp_obj_get_float(args[4]);

    size_t n;
    mp_obj_t *items;
    mp_obj_get_array(args[5], &n, &items);
    if(n < 1 || n > (size_t)gradient_brush_t::max_stops) {
      mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("invalid parameter, gradient expects 1 to 16 colour stops"));
    }

    float positions[gradient_brush_t::max_stops];
    uint32_t colors[gradient_brush_t::max_stops];
    for(size_t i = 0; i < n; i++) {
      size_t sl;
      mp_obj_t *stop;
      mp_obj_get_array(items[i], &sl, &stop);
      if(sl != 2 || !mp_obj_is_type(stop[1], &type_color)) {
        mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("invalid parameter, each stop must be (position, color)"));
      }
      positions[i] = mp_obj_get_float(stop[0]);
      colors[i] = ((color_obj_t *)MP_OBJ_TO_PTR(stop[1]))->c->_p;
    }

    mat3_t *m = nullptr;
    if(n_args >= 7 && mp_obj_is_type(args[6], &type_mat3)) {
      m = &((mat3_obj_t *)MP_OBJ_TO_PTR(args[6]))->m;
    }

    brush_obj_t *brush = mp_obj_malloc(brush_obj_t, &type_brush);
    brush->brush = m_new_class(gradient_brush_t, (gradient_type_t)gtype, x1, y1, x2, y2,
                               positions, colors, (int)n, m);
    return MP_OBJ_FROM_PTR(brush);
  })


  MPY_BIND_STATICMETHOD_VAR(1, pixelate, {
    // pixelate(size): mosaic the shape's area from the target, block size in px
    int size = mp_obj_get_int(args[0]);
    if(size < 1) size = 1;
    brush_obj_t *brush = mp_obj_malloc(brush_obj_t, &type_brush);
    brush->brush = m_new_class(pixelate_brush_t, size);
    return MP_OBJ_FROM_PTR(brush);
  })


  MPY_BIND_STATICMETHOD_VAR(1, blur, {
    // blur(radius): box-blur the shape's area from the target
    int radius = mp_obj_get_int(args[0]);
    if(radius < 1) radius = 1;
    brush_obj_t *brush = mp_obj_malloc(brush_obj_t, &type_brush);
    brush->brush = m_new_class(blur_brush_t, radius);
    return MP_OBJ_FROM_PTR(brush);
  })


  MPY_BIND_STATICMETHOD_VAR(1, lighten, {
    // lighten(amount): add `amount` (0..255) to each channel of the backdrop
    int amount = mp_obj_get_int(args[0]);
    brush_obj_t *brush = mp_obj_malloc(brush_obj_t, &type_brush);
    brush->brush = m_new_class(brightness_brush_t, amount);
    return MP_OBJ_FROM_PTR(brush);
  })


  MPY_BIND_STATICMETHOD_VAR(1, darken, {
    // darken(amount): subtract `amount` (0..255) from each channel of the backdrop
    int amount = mp_obj_get_int(args[0]);
    brush_obj_t *brush = mp_obj_malloc(brush_obj_t, &type_brush);
    brush->brush = m_new_class(brightness_brush_t, -amount);
    return MP_OBJ_FROM_PTR(brush);
  })


  MPY_BIND_LOCALS_DICT(brush,
    // MPY_BIND_ROM_PTR_DEL(brush),
    // MPY_BIND_ROM_PTR_STATIC(xor),
    // MPY_BIND_ROM_PTR_STATIC(brighten),
    { MP_ROM_QSTR(MP_QSTR_LINEAR), MP_ROM_INT(GRADIENT_LINEAR) },
    { MP_ROM_QSTR(MP_QSTR_RADIAL), MP_ROM_INT(GRADIENT_RADIAL) },
    MPY_BIND_ROM_PTR_STATIC(pattern),
    MPY_BIND_ROM_PTR_STATIC(image),
    MPY_BIND_ROM_PTR_STATIC(gradient),
    MPY_BIND_ROM_PTR_STATIC(pixelate),
    MPY_BIND_ROM_PTR_STATIC(blur),
    MPY_BIND_ROM_PTR_STATIC(lighten),
    MPY_BIND_ROM_PTR_STATIC(darken),
  )


  MP_DEFINE_CONST_OBJ_TYPE(
      type_brush,
      MP_QSTR_brush,
      MP_TYPE_FLAG_NONE,
      locals_dict, &brush_locals_dict
  );


}
