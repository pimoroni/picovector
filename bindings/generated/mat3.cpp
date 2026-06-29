// AUTO-GENERATED from api/mat3.py by generate.py — do not edit by hand.
#include "pv_bindings.hpp"
#include "types.h"

extern "C" {

// mat3.rotate: Rotate by degrees. Returns a new mat3.
mp_obj_t mpy_mat3_rotate(size_t n_args, const mp_obj_t *args) {
  self(args[0], mat3_obj_t);
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_mat3_rotate);
#endif
  size_t _i = 1;
  float degrees = mp_obj_get_float(args[_i]); _i++;
  return pv::box_mat3(self->m.rotate(degrees));
}

// mat3.rotate_radians: Rotate by radians. Returns a new mat3.
mp_obj_t mpy_mat3_rotate_radians(size_t n_args, const mp_obj_t *args) {
  self(args[0], mat3_obj_t);
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_mat3_rotate_radians);
#endif
  size_t _i = 1;
  float radians = mp_obj_get_float(args[_i]); _i++;
  return pv::box_mat3(self->m.rotate_radians(radians));
}

// mat3.translate: Translate by (x, y). Also accepts a single vec2. Returns a new mat3.
mp_obj_t mpy_mat3_translate(size_t n_args, const mp_obj_t *args) {
  self(args[0], mat3_obj_t);
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_mat3_translate);
#endif
  size_t _i = 1;
  vec2_t p = pv::get_xy(args, &_i, n_args);
  return pv::box_mat3(self->m.translate(p.x, p.y));
}

// mat3.scale: Scale by (x, y). Pass one value to scale uniformly. Returns a new mat3.
mp_obj_t mpy_mat3_scale(size_t n_args, const mp_obj_t *args) {
  self(args[0], mat3_obj_t);
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_mat3_scale);
#endif
  size_t _i = 1;
  float x = mp_obj_get_float(args[_i]); _i++;
  float y = x;
  if (n_args > _i) { y = mp_obj_get_float(args[_i]); _i++; }
  return pv::box_mat3(self->m.scale(x, y));
}

// mat3.multiply: Multiply this matrix by another mat3. Returns a new mat3.
mp_obj_t mpy_mat3_multiply(size_t n_args, const mp_obj_t *args) {
  self(args[0], mat3_obj_t);
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_mat3_multiply);
#endif
  size_t _i = 1;
  mat3_t other = ((mat3_obj_t *)MP_OBJ_TO_PTR(args[_i]))->m; _i++;
  return pv::box_mat3(self->m.multiply(other));
}

// mat3.inverse: Return the inverse of this matrix.
mp_obj_t mpy_mat3_inverse(size_t n_args, const mp_obj_t *args) {
  self(args[0], mat3_obj_t);
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_mat3_inverse);
#endif
  return pv::box_mat3(self->m.inverse());
}

static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_mat3_rotate_obj, 2, mpy_mat3_rotate);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_mat3_rotate_radians_obj, 2, mpy_mat3_rotate_radians);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_mat3_translate_obj, 2, mpy_mat3_translate);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_mat3_scale_obj, 2, mpy_mat3_scale);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_mat3_multiply_obj, 2, mpy_mat3_multiply);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_mat3_inverse_obj, 1, mpy_mat3_inverse);

static mp_obj_t mat3_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
  mat3_obj_t *self = mp_obj_malloc(mat3_obj_t, type);
  self->m = mat3_t();
  return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t mat3_binary_op(mp_binary_op_t op, mp_obj_t lhs_in, mp_obj_t rhs_in) {
  mat3_obj_t *lhs = (mat3_obj_t *)MP_OBJ_TO_PTR(lhs_in);
  switch (op) {
    case MP_BINARY_OP_MULTIPLY: {
      if (mp_obj_is_type(rhs_in, &type_mat3)) {
        mat3_obj_t *rhs = (mat3_obj_t *)MP_OBJ_TO_PTR(rhs_in);
        return pv::box_mat3(lhs->m.multiply(rhs->m));
      }
    } break;
    default: break;  // unhandled ops fall through to MP_OBJ_NULL
  }
  return MP_OBJ_NULL;
}

static const mp_rom_map_elem_t mat3_locals_dict_table[] = {
  { MP_ROM_QSTR(MP_QSTR_rotate), MP_ROM_PTR(&mpy_mat3_rotate_obj) },
  { MP_ROM_QSTR(MP_QSTR_rotate_radians), MP_ROM_PTR(&mpy_mat3_rotate_radians_obj) },
  { MP_ROM_QSTR(MP_QSTR_translate), MP_ROM_PTR(&mpy_mat3_translate_obj) },
  { MP_ROM_QSTR(MP_QSTR_scale), MP_ROM_PTR(&mpy_mat3_scale_obj) },
  { MP_ROM_QSTR(MP_QSTR_multiply), MP_ROM_PTR(&mpy_mat3_multiply_obj) },
  { MP_ROM_QSTR(MP_QSTR_inverse), MP_ROM_PTR(&mpy_mat3_inverse_obj) },
};
static MP_DEFINE_CONST_DICT(mat3_locals_dict, mat3_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
  type_mat3,
  MP_QSTR_mat3,
  MP_TYPE_FLAG_NONE,
  make_new, (const void *)mat3_make_new,
  binary_op, (const void *)mat3_binary_op,
  locals_dict, &mat3_locals_dict
);

}
