// AUTO-GENERATED from api/brush.py by generate.py — do not edit by hand.
#include "pv_bindings.hpp"
#include "types.h"

extern "C" {

// brush.pattern: Checkerboard pattern brush. Args: c1, c2 (colors), index (0–37) or 8-tuple.
mp_obj_t mpy_brush_pattern(size_t n_args, const mp_obj_t *args) {
  if (n_args == 3 && mp_obj_is_type(args[0], &type_color) && mp_obj_is_type(args[1], &type_color) && mp_obj_is_int(args[2])) {
    size_t _i = 0;
    color_t c1 = (*((color_obj_t *)MP_OBJ_TO_PTR(args[_i]))->c); _i++;
    color_t c2 = (*((color_obj_t *)MP_OBJ_TO_PTR(args[_i]))->c); _i++;
    int index = (int)mp_obj_get_float(args[_i]); _i++;
    if (index < 0 || index > 37) mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("index out of range"));
    return pv::box_brush(m_new_class(pattern_brush_t, c1, c2, index));
  }
  if (n_args == 3 && mp_obj_is_type(args[0], &type_color) && mp_obj_is_type(args[1], &type_color) && mp_obj_is_type(args[2], &mp_type_tuple)) {
    size_t _i = 0;
    color_t c1 = (*((color_obj_t *)MP_OBJ_TO_PTR(args[_i]))->c); _i++;
    color_t c2 = (*((color_obj_t *)MP_OBJ_TO_PTR(args[_i]))->c); _i++;
    size_t pattern_len; mp_obj_t *pattern_items;
    mp_obj_get_array(args[_i], &pattern_len, &pattern_items); _i++;
    if (pattern_len != 8) mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("custom pattern must be a tuple with 8 elements"));
    uint8_t pattern[8];
    for (int _b = 0; _b < 8; _b++) pattern[_b] = mp_obj_get_int(pattern_items[_b]);
    return pv::box_brush(m_new_class(pattern_brush_t, c1, c2, pattern));
  }
  mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("invalid parameter, expected brush.pattern(color, color, index | tuple[8])"));
}

// brush.image: Image brush. Args: img (image). Optional: transform (mat3).
mp_obj_t mpy_brush_image(size_t n_args, const mp_obj_t *args) {
  if (n_args == 1 && mp_obj_is_type(args[0], &type_image)) {
    size_t _i = 0;
    image_t * img = ((image_obj_t *)MP_OBJ_TO_PTR(args[_i]))->image; _i++;
    return pv::box_brush(m_new_class(image_brush_t, img));
  }
  if (n_args == 2 && mp_obj_is_type(args[0], &type_image) && mp_obj_is_type(args[1], &type_mat3)) {
    size_t _i = 0;
    image_t * img = ((image_obj_t *)MP_OBJ_TO_PTR(args[_i]))->image; _i++;
    mat3_t transform = ((mat3_obj_t *)MP_OBJ_TO_PTR(args[_i]))->m; _i++;
    return pv::box_brush(m_new_class(image_brush_t, img, &transform));
  }
  mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("invalid parameter, expected brush.image(image, [mat3])"));
}

// brush.gradient: Gradient brush. type: brush.LINEAR or brush.RADIAL; stops: list of
mp_obj_t mpy_brush_gradient(size_t n_args, const mp_obj_t *args) {
  size_t _i = 0;
  int type = (int)mp_obj_get_float(args[_i]); _i++;
  float x1 = mp_obj_get_float(args[_i]); _i++;
  float y1 = mp_obj_get_float(args[_i]); _i++;
  float x2 = mp_obj_get_float(args[_i]); _i++;
  float y2 = mp_obj_get_float(args[_i]); _i++;
  size_t stops_n; mp_obj_t *stops_items;
  mp_obj_get_array(args[_i], &stops_n, &stops_items); _i++;
  if (stops_n < 1 || stops_n > 16) mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("gradient expects 1 to 16 colour stops"));
  float stops_positions[16]; uint32_t stops_colors[16];
  for (size_t _s = 0; _s < stops_n; _s++) {
    size_t _sl; mp_obj_t *_stop; mp_obj_get_array(stops_items[_s], &_sl, &_stop);
    if (_sl != 2 || !mp_obj_is_type(_stop[1], &type_color)) mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("each stop must be (position, color)"));
    stops_positions[_s] = mp_obj_get_float(_stop[0]);
    stops_colors[_s] = ((color_obj_t *)MP_OBJ_TO_PTR(_stop[1]))->c->_p;
  }
  mat3_t * transform = nullptr;
  if (n_args > _i && mp_obj_is_type(args[_i], &type_mat3)) { transform = &((mat3_obj_t *)MP_OBJ_TO_PTR(args[_i]))->m; _i++; }
  return pv::box_brush(m_new_class(gradient_brush_t, (gradient_type_t)type, x1, y1, x2, y2, stops_positions, stops_colors, stops_n, transform));
}

// brush.pixelate: Mosaic the shape's area, block size in pixels.
mp_obj_t mpy_brush_pixelate(size_t n_args, const mp_obj_t *args) {
  size_t _i = 0;
  int size = (int)mp_obj_get_float(args[_i]); _i++;
  if (size < 1) size = 1;
  return pv::box_brush(m_new_class(pixelate_brush_t, size));
}

// brush.blur: Box-blur the shape's area from the target.
mp_obj_t mpy_brush_blur(size_t n_args, const mp_obj_t *args) {
  size_t _i = 0;
  int radius = (int)mp_obj_get_float(args[_i]); _i++;
  if (radius < 1) radius = 1;
  return pv::box_brush(m_new_class(blur_brush_t, radius));
}

// brush.lighten: Add amount (0–255) to each channel of the backdrop.
mp_obj_t mpy_brush_lighten(size_t n_args, const mp_obj_t *args) {
  size_t _i = 0;
  int amount = (int)mp_obj_get_float(args[_i]); _i++;
  return pv::box_brush(m_new_class(brightness_brush_t, amount));
}

// brush.darken: Subtract amount (0–255) from each channel of the backdrop.
mp_obj_t mpy_brush_darken(size_t n_args, const mp_obj_t *args) {
  size_t _i = 0;
  int amount = (int)mp_obj_get_float(args[_i]); _i++;
  return pv::box_brush(m_new_class(brightness_brush_t, -amount));
}

static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_brush_pattern_obj, 0, mpy_brush_pattern);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_brush_pattern_static_obj, MP_ROM_PTR(&mpy_brush_pattern_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_brush_image_obj, 0, mpy_brush_image);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_brush_image_static_obj, MP_ROM_PTR(&mpy_brush_image_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_brush_gradient_obj, 6, mpy_brush_gradient);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_brush_gradient_static_obj, MP_ROM_PTR(&mpy_brush_gradient_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_brush_pixelate_obj, 1, mpy_brush_pixelate);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_brush_pixelate_static_obj, MP_ROM_PTR(&mpy_brush_pixelate_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_brush_blur_obj, 1, mpy_brush_blur);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_brush_blur_static_obj, MP_ROM_PTR(&mpy_brush_blur_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_brush_lighten_obj, 1, mpy_brush_lighten);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_brush_lighten_static_obj, MP_ROM_PTR(&mpy_brush_lighten_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_brush_darken_obj, 1, mpy_brush_darken);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_brush_darken_static_obj, MP_ROM_PTR(&mpy_brush_darken_obj));

static const mp_rom_map_elem_t brush_locals_dict_table[] = {
  { MP_ROM_QSTR(MP_QSTR_LINEAR), MP_ROM_INT(GRADIENT_LINEAR) },
  { MP_ROM_QSTR(MP_QSTR_RADIAL), MP_ROM_INT(GRADIENT_RADIAL) },
  { MP_ROM_QSTR(MP_QSTR_pattern), MP_ROM_PTR(&mpy_brush_pattern_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_image), MP_ROM_PTR(&mpy_brush_image_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_gradient), MP_ROM_PTR(&mpy_brush_gradient_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_pixelate), MP_ROM_PTR(&mpy_brush_pixelate_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_blur), MP_ROM_PTR(&mpy_brush_blur_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_lighten), MP_ROM_PTR(&mpy_brush_lighten_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_darken), MP_ROM_PTR(&mpy_brush_darken_static_obj) },
};
static MP_DEFINE_CONST_DICT(brush_locals_dict, brush_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
  type_brush,
  MP_QSTR_brush,
  MP_TYPE_FLAG_NONE,
  locals_dict, &brush_locals_dict
);

}
