// AUTO-GENERATED from api/algorithm.py by generate.py — do not edit by hand.
#include "pv_bindings.hpp"
#include "types.h"

extern "C" {

extern "C" mp_obj_t algorithm_clip_line(size_t n_args, const mp_obj_t *args);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_algorithm_clip_line_obj, 3, algorithm_clip_line);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_algorithm_clip_line_static_obj, MP_ROM_PTR(&mpy_algorithm_clip_line_obj));
extern "C" mp_obj_t algorithm_dda(size_t n_args, const mp_obj_t *args);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_algorithm_dda_obj, 3, algorithm_dda);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_algorithm_dda_static_obj, MP_ROM_PTR(&mpy_algorithm_dda_obj));
extern "C" mp_obj_t algorithm_raycast(size_t n_args, const mp_obj_t *args);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_algorithm_raycast_obj, 9, algorithm_raycast);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_algorithm_raycast_static_obj, MP_ROM_PTR(&mpy_algorithm_raycast_obj));

static const mp_rom_map_elem_t algorithm_locals_dict_table[] = {
  { MP_ROM_QSTR(MP_QSTR_clip_line), MP_ROM_PTR(&mpy_algorithm_clip_line_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_dda), MP_ROM_PTR(&mpy_algorithm_dda_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_raycast), MP_ROM_PTR(&mpy_algorithm_raycast_static_obj) },
};
static MP_DEFINE_CONST_DICT(algorithm_locals_dict, algorithm_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
  type_algorithm,
  MP_QSTR_algorithm,
  MP_TYPE_FLAG_NONE,
  locals_dict, &algorithm_locals_dict
);

}
