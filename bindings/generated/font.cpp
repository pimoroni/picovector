// AUTO-GENERATED from api/font.py by generate.py — do not edit by hand.
#include "pv_bindings.hpp"
#include "types.h"

extern "C" {

extern "C" mp_obj_t font_load(size_t n_args, const mp_obj_t *args);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_font_load_obj, 1, font_load);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_font_load_static_obj, MP_ROM_PTR(&mpy_font_load_obj));

extern "C" mp_obj_t font__del__(mp_obj_t self_in);
static MP_DEFINE_CONST_FUN_OBJ_1(font__del___obj, font__del__);

static void font_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
  mp_printf(print, "font()");
}

static const mp_rom_map_elem_t font_locals_dict_table[] = {
  { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&font__del___obj) },
  { MP_ROM_QSTR(MP_QSTR_load), MP_ROM_PTR(&mpy_font_load_static_obj) },
};
static MP_DEFINE_CONST_DICT(font_locals_dict, font_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
  type_font,
  MP_QSTR_font,
  MP_TYPE_FLAG_NONE,
  print, (const void *)font_print,
  locals_dict, &font_locals_dict
);

}
