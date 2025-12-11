#include "picovector.hpp"
#include "mp_helpers.hpp"

action_t m_attr_action(mp_obj_t *dest) {
  if(dest[0] == MP_OBJ_NULL && dest[1] == MP_OBJ_NULL) {return GET;}
  if(dest[0] == MP_OBJ_NULL && dest[1] != MP_OBJ_NULL) {return DELETE;}
  return SET;
}

uint32_t ru32(mp_obj_t file) {
  int error;
  uint32_t result;
  mp_stream_read_exactly(file, &result, 4, &error);
  return __builtin_bswap32(result);
}

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

extern "C" {
  #include "py/runtime.h"
  image_obj_t *default_target;

  mp_obj_t modpicovector___init__(void) {
      return mp_const_none;
  }

  brush_obj_t *mp_obj_to_brush(size_t n_args, const mp_obj_t *args) {
    if(n_args == 1 && mp_obj_is_type(args[0], &type_brush)) {
      return (brush_obj_t *)args[0];
    }

    if(n_args == 1 && mp_obj_is_type(args[0], &type_color)) {
      color_obj_t *color = (color_obj_t *)MP_OBJ_TO_PTR(args[0]);
      brush_obj_t *brush = mp_obj_malloc(brush_obj_t, &type_brush);
      brush->brush = m_new_class(color_brush, color->c);
      return brush;
    }
    if(n_args >= 3 && mp_obj_is_int(args[0]) && mp_obj_is_int(args[1]) && mp_obj_is_int(args[2])) {
      brush_obj_t *brush = mp_obj_malloc(brush_obj_t, &type_brush);
      int r = mp_obj_get_int(args[0]);
      int g = mp_obj_get_int(args[1]);
      int b = mp_obj_get_int(args[2]);
      int a = (n_args > 3 && mp_obj_is_int(args[3])) ? mp_obj_get_int(args[3]) : 255;
      brush->brush = m_new_class(color_brush, r, g, b, a);
      return brush;
    }

    return nullptr;
  }

  mp_obj_t modpicovector_pen(size_t n_args, const mp_obj_t *args) {
    brush_obj_t *new_brush = mp_obj_to_brush(n_args, args);

    if(!new_brush){
      mp_raise_TypeError(MP_ERROR_TEXT("value must be of type brush or color"));
    }

    // TODO: This should set a GLOBAL brush along with other state on
    // picovector, and the default target (an image) should then use the
    // global brush for painting
    if(default_target) {
      default_target->brush = new_brush;
      default_target->image->brush(default_target->brush->brush);
    }
    return mp_const_none;
  }



  void modpicovector_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    size_t action = m_attr_action(dest);

    switch(attr) {
      case MP_QSTR_default_target: {
        if(action == SET) {
          if(!mp_obj_is_type(dest[1], &type_image)) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("invalid parameter, expected image"));
          }
          default_target = (image_obj_t *)dest[1];
          dest[0] = MP_OBJ_NULL;
          return;
        } else {
          dest[0] = MP_OBJ_FROM_PTR(default_target);
        }
      };
    }
  }


}
