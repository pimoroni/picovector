// AUTO-GENERATED from api/pixel_font.py by generate.py — do not edit by hand.
#include "pv_bindings.hpp"
#include "types.h"

extern "C" {

extern "C" mp_obj_t pixel_font_load(size_t n_args, const mp_obj_t *args);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_pixel_font_load_obj, 1, pixel_font_load);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_pixel_font_load_static_obj, MP_ROM_PTR(&mpy_pixel_font_load_obj));

extern "C" mp_obj_t pixel_font__del__(mp_obj_t self_in);
static MP_DEFINE_CONST_FUN_OBJ_1(pixel_font__del___obj, pixel_font__del__);

static void pixel_font_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
  mp_printf(print, "pixel_font()");
}

void pixel_font_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
  self(self_in, pixel_font_obj_t);
  action_t action = m_attr_action(dest);
  switch (attr) {
    case MP_QSTR_height:
    {
      if (action == GET) { dest[0] = mp_obj_new_int(self->font->height); return; }
    }
    case MP_QSTR_name:
    {
      if (action == GET) { dest[0] = mp_obj_new_str(self->font->name, strlen(self->font->name)); return; }
    }
  }
  dest[1] = MP_OBJ_SENTINEL;
}

static const mp_rom_map_elem_t pixel_font_locals_dict_table[] = {
  { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&pixel_font__del___obj) },
  { MP_ROM_QSTR(MP_QSTR_load), MP_ROM_PTR(&mpy_pixel_font_load_static_obj) },
};
static MP_DEFINE_CONST_DICT(pixel_font_locals_dict, pixel_font_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
  type_pixel_font,
  MP_QSTR_pixel_font,
  MP_TYPE_FLAG_NONE,
  print, (const void *)pixel_font_print,
  attr, (const void *)pixel_font_attr,
  locals_dict, &pixel_font_locals_dict
);

}
