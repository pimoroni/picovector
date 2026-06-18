// AUTO-GENERATED from api/vec2.py by generate.py — do not edit by hand.
#include "pv_bindings.hpp"
#include "types.h"

extern "C" {

// vec2.length: Magnitude (Euclidean length) of this vector.
mp_obj_t mpy_vec2_length(size_t n_args, const mp_obj_t *args) {
  self(args[0], vec2_obj_t);
  return mp_obj_new_float(self->v.length());
}

// vec2.length_squared: Squared magnitude (faster than length() — no square root).
mp_obj_t mpy_vec2_length_squared(size_t n_args, const mp_obj_t *args) {
  self(args[0], vec2_obj_t);
  return mp_obj_new_float(self->v.length_squared());
}

// vec2.dot: Dot product with another vec2.
mp_obj_t mpy_vec2_dot(size_t n_args, const mp_obj_t *args) {
  self(args[0], vec2_obj_t);
  size_t _i = 1;
  vec2_t other = mp_obj_get_vec2(args[_i]); _i++;
  return mp_obj_new_float(self->v.dot(other));
}

// vec2.cross: Cross product (scalar z-component) with another vec2.
mp_obj_t mpy_vec2_cross(size_t n_args, const mp_obj_t *args) {
  self(args[0], vec2_obj_t);
  size_t _i = 1;
  vec2_t other = mp_obj_get_vec2(args[_i]); _i++;
  return mp_obj_new_float(self->v.cross(other));
}

// vec2.distance: Euclidean distance to another vec2.
mp_obj_t mpy_vec2_distance(size_t n_args, const mp_obj_t *args) {
  self(args[0], vec2_obj_t);
  size_t _i = 1;
  vec2_t other = mp_obj_get_vec2(args[_i]); _i++;
  return mp_obj_new_float(self->v.distance(other));
}

// vec2.distance_squared: Squared distance to another vec2 (faster than distance()).
mp_obj_t mpy_vec2_distance_squared(size_t n_args, const mp_obj_t *args) {
  self(args[0], vec2_obj_t);
  size_t _i = 1;
  vec2_t other = mp_obj_get_vec2(args[_i]); _i++;
  return mp_obj_new_float(self->v.distance_squared(other));
}

// vec2.angle: Angle of this vector in radians (atan2(y, x)).
mp_obj_t mpy_vec2_angle(size_t n_args, const mp_obj_t *args) {
  self(args[0], vec2_obj_t);
  return mp_obj_new_float(self->v.angle());
}

// vec2.angle_to: Angle from this vector to another, in radians.
mp_obj_t mpy_vec2_angle_to(size_t n_args, const mp_obj_t *args) {
  self(args[0], vec2_obj_t);
  size_t _i = 1;
  vec2_t other = mp_obj_get_vec2(args[_i]); _i++;
  return mp_obj_new_float(self->v.angle_to(other));
}

// vec2.normalized: Return a unit vector in the same direction.
mp_obj_t mpy_vec2_normalized(size_t n_args, const mp_obj_t *args) {
  self(args[0], vec2_obj_t);
  return pv::box_vec2(self->v.normalized());
}

// vec2.perpendicular: Return a vector perpendicular to this one (rotated 90°).
mp_obj_t mpy_vec2_perpendicular(size_t n_args, const mp_obj_t *args) {
  self(args[0], vec2_obj_t);
  return pv::box_vec2(self->v.perpendicular());
}

// vec2.abs: Return component-wise absolute value.
mp_obj_t mpy_vec2_abs(size_t n_args, const mp_obj_t *args) {
  self(args[0], vec2_obj_t);
  return pv::box_vec2(self->v.abs());
}

// vec2.rotated: Return this vector rotated by angle (radians).
mp_obj_t mpy_vec2_rotated(size_t n_args, const mp_obj_t *args) {
  self(args[0], vec2_obj_t);
  size_t _i = 1;
  float angle = mp_obj_get_float(args[_i]); _i++;
  return pv::box_vec2(self->v.rotated(angle));
}

// vec2.lerp: Linear interpolation toward other. t=0 returns self, t=1 returns other.
mp_obj_t mpy_vec2_lerp(size_t n_args, const mp_obj_t *args) {
  self(args[0], vec2_obj_t);
  size_t _i = 1;
  vec2_t other = mp_obj_get_vec2(args[_i]); _i++;
  float t = mp_obj_get_float(args[_i]); _i++;
  return pv::box_vec2(self->v.lerp(other, t));
}

// vec2.reflect: Reflect this vector around the given normal vec2.
mp_obj_t mpy_vec2_reflect(size_t n_args, const mp_obj_t *args) {
  self(args[0], vec2_obj_t);
  size_t _i = 1;
  vec2_t normal = mp_obj_get_vec2(args[_i]); _i++;
  return pv::box_vec2(self->v.reflect(normal));
}

// vec2.clamp_length: Return this vector clamped to at most max_length magnitude.
mp_obj_t mpy_vec2_clamp_length(size_t n_args, const mp_obj_t *args) {
  self(args[0], vec2_obj_t);
  size_t _i = 1;
  float max_length = mp_obj_get_float(args[_i]); _i++;
  return pv::box_vec2(self->v.clamp_length(max_length));
}

// vec2.transform: Apply a mat3 transformation to this vector, in place.
mp_obj_t mpy_vec2_transform(size_t n_args, const mp_obj_t *args) {
  self(args[0], vec2_obj_t);
  size_t _i = 1;
  mat3_t m = ((mat3_obj_t *)MP_OBJ_TO_PTR(args[_i]))->m; _i++;
  self->v = self->v.transform(m);
  return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_vec2_length_obj, 1, mpy_vec2_length);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_vec2_length_squared_obj, 1, mpy_vec2_length_squared);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_vec2_dot_obj, 2, mpy_vec2_dot);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_vec2_cross_obj, 2, mpy_vec2_cross);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_vec2_distance_obj, 2, mpy_vec2_distance);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_vec2_distance_squared_obj, 2, mpy_vec2_distance_squared);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_vec2_angle_obj, 1, mpy_vec2_angle);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_vec2_angle_to_obj, 2, mpy_vec2_angle_to);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_vec2_normalized_obj, 1, mpy_vec2_normalized);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_vec2_perpendicular_obj, 1, mpy_vec2_perpendicular);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_vec2_abs_obj, 1, mpy_vec2_abs);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_vec2_rotated_obj, 2, mpy_vec2_rotated);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_vec2_lerp_obj, 3, mpy_vec2_lerp);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_vec2_reflect_obj, 2, mpy_vec2_reflect);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_vec2_clamp_length_obj, 2, mpy_vec2_clamp_length);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_vec2_transform_obj, 2, mpy_vec2_transform);

static mp_obj_t vec2_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
  vec2_obj_t *self = mp_obj_malloc(vec2_obj_t, type);
  if (n_args == 0) {
  }
  else if (n_args == 2) {
    self->v.x = mp_obj_get_float(args[0]);
    self->v.y = mp_obj_get_float(args[1]);
  }
  else mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("invalid parameters"));
  return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t vec2_binary_op(mp_binary_op_t op, mp_obj_t lhs_in, mp_obj_t rhs_in) {
  vec2_obj_t *lhs = (vec2_obj_t *)MP_OBJ_TO_PTR(lhs_in);
  switch (op) {
    case MP_BINARY_OP_ADD: {
      if (mp_obj_is_type(rhs_in, &type_vec2)) {
        vec2_obj_t *rhs = (vec2_obj_t *)MP_OBJ_TO_PTR(rhs_in);
        return pv::box_vec2(lhs->v + rhs->v);
      }
    } break;
    case MP_BINARY_OP_SUBTRACT: {
      if (mp_obj_is_type(rhs_in, &type_vec2)) {
        vec2_obj_t *rhs = (vec2_obj_t *)MP_OBJ_TO_PTR(rhs_in);
        return pv::box_vec2(lhs->v - rhs->v);
      }
    } break;
    case MP_BINARY_OP_MULTIPLY: {
      if (mp_obj_is_type(rhs_in, &type_vec2)) {
        vec2_obj_t *rhs = (vec2_obj_t *)MP_OBJ_TO_PTR(rhs_in);
        return pv::box_vec2(lhs->v * rhs->v);
      }
      if (mp_obj_is_int(rhs_in) || mp_obj_is_float(rhs_in)) {
        float v = mp_obj_get_float(rhs_in);
        return pv::box_vec2(lhs->v * v);
      }
    } break;
    case MP_BINARY_OP_TRUE_DIVIDE: {
      if (mp_obj_is_type(rhs_in, &type_vec2)) {
        vec2_obj_t *rhs = (vec2_obj_t *)MP_OBJ_TO_PTR(rhs_in);
        return pv::box_vec2(lhs->v / rhs->v);
      }
      if (mp_obj_is_int(rhs_in) || mp_obj_is_float(rhs_in)) {
        float v = mp_obj_get_float(rhs_in);
        return pv::box_vec2(lhs->v / v);
      }
    } break;
    case MP_BINARY_OP_EQUAL: {
      if (mp_obj_is_type(rhs_in, &type_vec2)) {
        vec2_obj_t *rhs = (vec2_obj_t *)MP_OBJ_TO_PTR(rhs_in);
        return mp_obj_new_bool(lhs->v == rhs->v);
      }
      return mp_const_false;
    } break;
    case MP_BINARY_OP_NOT_EQUAL: {
      if (mp_obj_is_type(rhs_in, &type_vec2)) {
        vec2_obj_t *rhs = (vec2_obj_t *)MP_OBJ_TO_PTR(rhs_in);
        return mp_obj_new_bool(lhs->v != rhs->v);
      }
      return mp_const_true;
    } break;
    default: break;  // unhandled ops fall through to MP_OBJ_NULL
  }
  return MP_OBJ_NULL;
}

static void vec2_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
  self(self_in, vec2_obj_t);
  mp_printf(print, "vec(%f, %f)", self->v.x, self->v.y);
}

void vec2_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
  self(self_in, vec2_obj_t);
  action_t action = m_attr_action(dest);
  switch (attr) {
    case MP_QSTR_x:
    {
      if (action == GET) { dest[0] = mp_obj_new_float(self->v.x); return; }
      if (action == SET) { self->v.x = mp_obj_get_float(dest[1]); dest[0] = MP_OBJ_NULL; return; }
    }
    case MP_QSTR_y:
    {
      if (action == GET) { dest[0] = mp_obj_new_float(self->v.y); return; }
      if (action == SET) { self->v.y = mp_obj_get_float(dest[1]); dest[0] = MP_OBJ_NULL; return; }
    }
  }
  dest[1] = MP_OBJ_SENTINEL;
}

static const mp_rom_map_elem_t vec2_locals_dict_table[] = {
  { MP_ROM_QSTR(MP_QSTR_length), MP_ROM_PTR(&mpy_vec2_length_obj) },
  { MP_ROM_QSTR(MP_QSTR_length_squared), MP_ROM_PTR(&mpy_vec2_length_squared_obj) },
  { MP_ROM_QSTR(MP_QSTR_dot), MP_ROM_PTR(&mpy_vec2_dot_obj) },
  { MP_ROM_QSTR(MP_QSTR_cross), MP_ROM_PTR(&mpy_vec2_cross_obj) },
  { MP_ROM_QSTR(MP_QSTR_distance), MP_ROM_PTR(&mpy_vec2_distance_obj) },
  { MP_ROM_QSTR(MP_QSTR_distance_squared), MP_ROM_PTR(&mpy_vec2_distance_squared_obj) },
  { MP_ROM_QSTR(MP_QSTR_angle), MP_ROM_PTR(&mpy_vec2_angle_obj) },
  { MP_ROM_QSTR(MP_QSTR_angle_to), MP_ROM_PTR(&mpy_vec2_angle_to_obj) },
  { MP_ROM_QSTR(MP_QSTR_normalized), MP_ROM_PTR(&mpy_vec2_normalized_obj) },
  { MP_ROM_QSTR(MP_QSTR_perpendicular), MP_ROM_PTR(&mpy_vec2_perpendicular_obj) },
  { MP_ROM_QSTR(MP_QSTR_abs), MP_ROM_PTR(&mpy_vec2_abs_obj) },
  { MP_ROM_QSTR(MP_QSTR_rotated), MP_ROM_PTR(&mpy_vec2_rotated_obj) },
  { MP_ROM_QSTR(MP_QSTR_lerp), MP_ROM_PTR(&mpy_vec2_lerp_obj) },
  { MP_ROM_QSTR(MP_QSTR_reflect), MP_ROM_PTR(&mpy_vec2_reflect_obj) },
  { MP_ROM_QSTR(MP_QSTR_clamp_length), MP_ROM_PTR(&mpy_vec2_clamp_length_obj) },
  { MP_ROM_QSTR(MP_QSTR_transform), MP_ROM_PTR(&mpy_vec2_transform_obj) },
};
static MP_DEFINE_CONST_DICT(vec2_locals_dict, vec2_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
  type_vec2,
  MP_QSTR_vec2,
  MP_TYPE_FLAG_NONE,
  make_new, (const void *)vec2_make_new,
  print, (const void *)vec2_print,
  binary_op, (const void *)vec2_binary_op,
  attr, (const void *)vec2_attr,
  locals_dict, &vec2_locals_dict
);

}
