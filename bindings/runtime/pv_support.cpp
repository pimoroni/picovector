// pv_support.cpp — shared, hand-written helpers used by the generated bindings.
//
// These are the small pieces of glue that aren't per-type bindings: the
// attr GET/SET discriminator, the big-endian file readers used by the font
// loaders, the vec2/rect argument helpers, the colour/brush coercion used by
// `image.pen`, and the module __init__.  They were previously spread across
// the hand-written picovector.cpp / vec2.cpp / rect.cpp; collecting them here
// keeps the generated .cpp files purely mechanical.

#include "pv_bindings.hpp"   // already pulls in picovector.hpp (no include guard)

using namespace picovector;

// ── attr action + file readers (C++ linkage, see pv_bindings.hpp) ────────────
action_t m_attr_action(mp_obj_t *dest) {
  if (dest[0] == MP_OBJ_NULL && dest[1] == MP_OBJ_NULL) { return GET; }
  if (dest[0] == MP_OBJ_NULL && dest[1] != MP_OBJ_NULL) { return DELETE; }
  return SET;
}

uint32_t ru32(mp_obj_t file) {
  int error; uint32_t result;
  mp_stream_read_exactly(file, &result, 4, &error);
  return __builtin_bswap32(result);
}
uint16_t ru16(mp_obj_t file) {
  int error; uint16_t result;
  mp_stream_read_exactly(file, &result, 2, &error);
  return __builtin_bswap16(result);
}
uint8_t ru8(mp_obj_t file) {
  int error; uint8_t result;
  mp_stream_read_exactly(file, &result, 1, &error);
  return result;
}
int8_t rs8(mp_obj_t file) {
  int error; int8_t result;
  mp_stream_read_exactly(file, &result, 1, &error);
  return result;
}

// ── vec2 / rect argument helpers (C++ linkage; declared in picovector.hpp) ───
bool mp_obj_is_vec2(mp_obj_t o) { return mp_obj_is_type(o, &type_vec2); }
bool mp_obj_is_rect(mp_obj_t o) { return mp_obj_is_type(o, &type_rect); }

vec2_t mp_obj_get_vec2(mp_obj_t vec2_in) {
  if (mp_obj_is_vec2(vec2_in)) {
    return ((vec2_obj_t *)MP_OBJ_TO_PTR(vec2_in))->v;
  }
  mp_raise_msg_varg(&mp_type_ValueError,
                    MP_ERROR_TEXT("invalid parameters, expected vec2(x, y)"));
}

vec2_t mp_obj_get_vec2_from_xy(const mp_obj_t *args) {
  return vec2_t(mp_obj_get_float(args[0]), mp_obj_get_float(args[1]));
}

rect_t mp_obj_get_rect(mp_obj_t rect_in) {
  if (mp_obj_is_rect(rect_in)) {
    return ((rect_obj_t *)MP_OBJ_TO_PTR(rect_in))->r;
  }
  mp_raise_msg_varg(&mp_type_ValueError,
                    MP_ERROR_TEXT("invalid parameters, expected rect(x, y, w, h)"));
}

rect_t mp_obj_get_rect_from_xywh(const mp_obj_t *args) {
  return rect_t(mp_obj_get_float(args[0]), mp_obj_get_float(args[1]),
                mp_obj_get_float(args[2]), mp_obj_get_float(args[3]));
}

extern "C" {

  #include "py/runtime.h"

  mp_obj_t modpicovector___init__(void) { return mp_const_none; }

  // Coerce a brush or colour argument into a brush object (used by image.pen).
  brush_obj_t *mp_obj_to_brush(size_t n_args, const mp_obj_t *args) {
    if (n_args == 1 && mp_obj_is_type(args[0], &type_brush)) {
      return (brush_obj_t *)MP_OBJ_TO_PTR(args[0]);
    }
    if (n_args == 1 && mp_obj_is_type(args[0], &type_color)) {
      color_obj_t *color = (color_obj_t *)MP_OBJ_TO_PTR(args[0]);
      brush_obj_t *brush = mp_obj_malloc(brush_obj_t, &type_brush);
      brush->brush = m_new_class(color_brush_t, color->c);
      return brush;
    }
    return nullptr;
  }

}
