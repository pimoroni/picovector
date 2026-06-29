// AUTO-GENERATED from api/color.py by generate.py — do not edit by hand.
#include "pv_bindings.hpp"
#include "types.h"
#include "blend.hpp"

extern "C" {

static color_obj_t _pv_pal(const color_t &c) {
  color_obj_t o{};
  o.base.type = &type_color;
  o.c = c;
  return o;
}

#ifdef TUFTY
static const color_obj_t color_black_obj = _pv_pal(rgb_color_t(0x14, 0x1e, 0x28, 0xff));
#else
static const color_obj_t color_black_obj = _pv_pal(rgb_color_t(0x00, 0x00, 0x00, 0xff));
#endif
static const color_obj_t color_grape_obj = _pv_pal(rgb_color_t(0x44, 0x24, 0x34, 0xff));
static const color_obj_t color_navy_obj = _pv_pal(rgb_color_t(0x30, 0x34, 0x6d, 0xff));
static const color_obj_t color_grey_obj = _pv_pal(rgb_color_t(0x4e, 0x4a, 0x4e, 0xff));
static const color_obj_t color_brown_obj = _pv_pal(rgb_color_t(0x85, 0x4c, 0x30, 0xff));
static const color_obj_t color_green_obj = _pv_pal(rgb_color_t(0x34, 0x65, 0x24, 0xff));
static const color_obj_t color_red_obj = _pv_pal(rgb_color_t(0xd0, 0x46, 0x48, 0xff));
static const color_obj_t color_taupe_obj = _pv_pal(rgb_color_t(0x75, 0x71, 0x61, 0xff));
static const color_obj_t color_blue_obj = _pv_pal(rgb_color_t(0x59, 0x7d, 0xce, 0xff));
static const color_obj_t color_orange_obj = _pv_pal(rgb_color_t(0xd2, 0x7d, 0x2c, 0xff));
static const color_obj_t color_smoke_obj = _pv_pal(rgb_color_t(0x85, 0x95, 0xa1, 0xff));
static const color_obj_t color_lime_obj = _pv_pal(rgb_color_t(0x6d, 0xaa, 0x2c, 0xff));
static const color_obj_t color_latte_obj = _pv_pal(rgb_color_t(0xd2, 0xaa, 0x99, 0xff));
static const color_obj_t color_cyan_obj = _pv_pal(rgb_color_t(0x6d, 0xc2, 0xca, 0xff));
static const color_obj_t color_yellow_obj = _pv_pal(rgb_color_t(0xda, 0xd4, 0x5e, 0xff));
#ifdef TUFTY
static const color_obj_t color_white_obj = _pv_pal(rgb_color_t(0xde, 0xee, 0xd6, 0xff));
#else
static const color_obj_t color_white_obj = _pv_pal(rgb_color_t(0xff, 0xff, 0xff, 0xff));
#endif
static const color_obj_t color_transparent_obj = _pv_pal(rgb_color_t(0x00, 0x00, 0x00, 0x00));
static const color_obj_t color_light_grey_obj = _pv_pal(rgb_color_t(0xc0, 0xc0, 0xc0, 0xff));
static const color_obj_t color_dark_grey_obj = _pv_pal(rgb_color_t(0x40, 0x40, 0x40, 0xff));

// color.rgb: Create a colour from RGB values (0–255 each). Optional alpha (0–255).
mp_obj_t mpy_color_rgb(size_t n_args, const mp_obj_t *args) {
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_color_rgb);
#endif
  size_t _i = 0;
  int r = (int)mp_obj_get_float(args[_i]); _i++;
  int g = (int)mp_obj_get_float(args[_i]); _i++;
  int b = (int)mp_obj_get_float(args[_i]); _i++;
  int a = 255;
  if (n_args > _i) { a = (int)mp_obj_get_float(args[_i]); _i++; }
  return pv::box_color(rgb_color_t(r, g, b, a));
}

// color.hsv: Create a colour from HSV components.
mp_obj_t mpy_color_hsv(size_t n_args, const mp_obj_t *args) {
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_color_hsv);
#endif
  size_t _i = 0;
  int h = (int)mp_obj_get_float(args[_i]); _i++;
  int s = (int)mp_obj_get_float(args[_i]); _i++;
  int v = (int)mp_obj_get_float(args[_i]); _i++;
  int a = 255;
  if (n_args > _i) { a = (int)mp_obj_get_float(args[_i]); _i++; }
  return pv::box_color(hsv_color_t((int)fmod((float)h,360.0f), s, v, a));
}

// color.oklch: Create a colour from OKLCH components.
mp_obj_t mpy_color_oklch(size_t n_args, const mp_obj_t *args) {
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_color_oklch);
#endif
  size_t _i = 0;
  int l = (int)mp_obj_get_float(args[_i]); _i++;
  int c = (int)mp_obj_get_float(args[_i]); _i++;
  int h = (int)mp_obj_get_float(args[_i]); _i++;
  int a = 255;
  if (n_args > _i) { a = (int)mp_obj_get_float(args[_i]); _i++; }
  return pv::box_color(oklch_color_t(l, c, h, a));
}

static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_color_rgb_obj, 3, mpy_color_rgb);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_color_rgb_static_obj, MP_ROM_PTR(&mpy_color_rgb_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_color_hsv_obj, 3, mpy_color_hsv);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_color_hsv_static_obj, MP_ROM_PTR(&mpy_color_hsv_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_color_oklch_obj, 3, mpy_color_oklch);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_color_oklch_static_obj, MP_ROM_PTR(&mpy_color_oklch_obj));

void color_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
  self(self_in, color_obj_t);
  action_t action = m_attr_action(dest);
  switch (attr) {
    case MP_QSTR_p:
    {
      if (action == GET) { dest[0] = mp_obj_new_int(self->c._p); return; }
    }
  }
  dest[1] = MP_OBJ_SENTINEL;
}

static const mp_rom_map_elem_t color_locals_dict_table[] = {
  { MP_ROM_QSTR(MP_QSTR_black), MP_ROM_PTR(&color_black_obj) },
  { MP_ROM_QSTR(MP_QSTR_grape), MP_ROM_PTR(&color_grape_obj) },
  { MP_ROM_QSTR(MP_QSTR_navy), MP_ROM_PTR(&color_navy_obj) },
  { MP_ROM_QSTR(MP_QSTR_grey), MP_ROM_PTR(&color_grey_obj) },
  { MP_ROM_QSTR(MP_QSTR_brown), MP_ROM_PTR(&color_brown_obj) },
  { MP_ROM_QSTR(MP_QSTR_green), MP_ROM_PTR(&color_green_obj) },
  { MP_ROM_QSTR(MP_QSTR_red), MP_ROM_PTR(&color_red_obj) },
  { MP_ROM_QSTR(MP_QSTR_taupe), MP_ROM_PTR(&color_taupe_obj) },
  { MP_ROM_QSTR(MP_QSTR_blue), MP_ROM_PTR(&color_blue_obj) },
  { MP_ROM_QSTR(MP_QSTR_orange), MP_ROM_PTR(&color_orange_obj) },
  { MP_ROM_QSTR(MP_QSTR_smoke), MP_ROM_PTR(&color_smoke_obj) },
  { MP_ROM_QSTR(MP_QSTR_lime), MP_ROM_PTR(&color_lime_obj) },
  { MP_ROM_QSTR(MP_QSTR_latte), MP_ROM_PTR(&color_latte_obj) },
  { MP_ROM_QSTR(MP_QSTR_cyan), MP_ROM_PTR(&color_cyan_obj) },
  { MP_ROM_QSTR(MP_QSTR_yellow), MP_ROM_PTR(&color_yellow_obj) },
  { MP_ROM_QSTR(MP_QSTR_white), MP_ROM_PTR(&color_white_obj) },
  { MP_ROM_QSTR(MP_QSTR_transparent), MP_ROM_PTR(&color_transparent_obj) },
  { MP_ROM_QSTR(MP_QSTR_light_grey), MP_ROM_PTR(&color_light_grey_obj) },
  { MP_ROM_QSTR(MP_QSTR_dark_grey), MP_ROM_PTR(&color_dark_grey_obj) },
  { MP_ROM_QSTR(MP_QSTR_rgb), MP_ROM_PTR(&mpy_color_rgb_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_hsv), MP_ROM_PTR(&mpy_color_hsv_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_oklch), MP_ROM_PTR(&mpy_color_oklch_static_obj) },
};
static MP_DEFINE_CONST_DICT(color_locals_dict, color_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
  type_color,
  MP_QSTR_color,
  MP_TYPE_FLAG_NONE,
  attr, (const void *)color_attr,
  locals_dict, &color_locals_dict
);

}
