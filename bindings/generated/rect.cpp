// AUTO-GENERATED from api/rect.py by generate.py — do not edit by hand.
#include "pv_bindings.hpp"
#include "types.h"

extern "C" {

// rect.deflate: Shrink by amount on each side, in place. deflate(a) or deflate(x, y).
mp_obj_t mpy_rect_deflate(size_t n_args, const mp_obj_t *args) {
  self(args[0], rect_obj_t);
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_rect_deflate);
#endif
  size_t _i = 1;
  float a1 = mp_obj_get_float(args[_i]); _i++;
  float a2 = a1;
  if (n_args > _i) { a2 = mp_obj_get_float(args[_i]); _i++; }
  self->r.deflate(a1, a2, a1, a2);
  return MP_OBJ_FROM_PTR(self);
}

// rect.inflate: Grow by amount on each side, in place. inflate(a) or inflate(x, y).
mp_obj_t mpy_rect_inflate(size_t n_args, const mp_obj_t *args) {
  self(args[0], rect_obj_t);
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_rect_inflate);
#endif
  size_t _i = 1;
  float a1 = mp_obj_get_float(args[_i]); _i++;
  float a2 = a1;
  if (n_args > _i) { a2 = mp_obj_get_float(args[_i]); _i++; }
  self->r.inflate(a1, a2, a1, a2);
  return MP_OBJ_FROM_PTR(self);
}

// rect.intersection: Return the intersection with another rect (empty if disjoint).
mp_obj_t mpy_rect_intersection(size_t n_args, const mp_obj_t *args) {
  self(args[0], rect_obj_t);
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_rect_intersection);
#endif
  size_t _i = 1;
  rect_t other = mp_obj_get_rect(args[_i]); _i++;
  return pv::box_rect(self->r.intersection(other));
}

// rect.intersects: True if this rect overlaps with other.
mp_obj_t mpy_rect_intersects(size_t n_args, const mp_obj_t *args) {
  self(args[0], rect_obj_t);
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_rect_intersects);
#endif
  size_t _i = 1;
  rect_t other = mp_obj_get_rect(args[_i]); _i++;
  return mp_obj_new_bool(self->r.intersects(other));
}

// rect.empty: True if this rect has zero area.
mp_obj_t mpy_rect_empty(size_t n_args, const mp_obj_t *args) {
  self(args[0], rect_obj_t);
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_rect_empty);
#endif
  return mp_obj_new_bool(self->r.empty());
}

// rect.offset: Shift by (x, y), in place. Also accepts a single vec2.
mp_obj_t mpy_rect_offset(size_t n_args, const mp_obj_t *args) {
  self(args[0], rect_obj_t);
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_rect_offset);
#endif
  size_t _i = 1;
  vec2_t p = pv::get_xy(args, &_i, n_args);
  self->r.offset(p.x, p.y);
  return MP_OBJ_FROM_PTR(self);
}

// rect.contains: True if this rect fully contains another rect or a point (vec2).
mp_obj_t mpy_rect_contains(size_t n_args, const mp_obj_t *args) {
  self(args[0], rect_obj_t);
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_rect_contains);
#endif
  if (n_args == 2 && mp_obj_is_type(args[1], &type_rect)) {
    size_t _i = 1;
    rect_t other = mp_obj_get_rect(args[_i]); _i++;
    return mp_obj_new_bool(self->r.contains(other));
  }
  if (n_args == 2 && mp_obj_is_type(args[1], &type_vec2)) {
    size_t _i = 1;
    vec2_t point = mp_obj_get_vec2(args[_i]); _i++;
    return mp_obj_new_bool(self->r.contains(point));
  }
  mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("invalid parameters, expected either rect(x, y, w, h) or vec2(x, y)"));
}

static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_rect_deflate_obj, 2, mpy_rect_deflate);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_rect_inflate_obj, 2, mpy_rect_inflate);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_rect_intersection_obj, 2, mpy_rect_intersection);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_rect_intersects_obj, 2, mpy_rect_intersects);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_rect_empty_obj, 1, mpy_rect_empty);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_rect_offset_obj, 2, mpy_rect_offset);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_rect_contains_obj, 1, mpy_rect_contains);

static mp_obj_t rect_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
  rect_obj_t *self = mp_obj_malloc_with_finaliser(rect_obj_t, type);
  if (n_args == 0) {
  }
  else if (n_args == 1) {
    self->r = mp_obj_get_rect(args[0]);
  }
  else if (n_args == 4) {
    self->r.x = mp_obj_get_float(args[0]);
    self->r.y = mp_obj_get_float(args[1]);
    self->r.w = mp_obj_get_float(args[2]);
    self->r.h = mp_obj_get_float(args[3]);
  }
  else mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("invalid parameters"));
  return MP_OBJ_FROM_PTR(self);
}

static void rect_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
  self(self_in, rect_obj_t);
  mp_printf(print, "rect(%f, %f -> %f x %f)", self->r.x, self->r.y, self->r.w, self->r.h);
}

void rect_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
  self(self_in, rect_obj_t);
  action_t action = m_attr_action(dest);
  switch (attr) {
    case MP_QSTR_x:
    case MP_QSTR_l:
    {
      if (action == GET) { dest[0] = mp_obj_new_float(self->r.x); return; }
      if (action == SET) { self->r.x = mp_obj_get_float(dest[1]); dest[0] = MP_OBJ_NULL; return; }
    }
    case MP_QSTR_y:
    case MP_QSTR_t:
    {
      if (action == GET) { dest[0] = mp_obj_new_float(self->r.y); return; }
      if (action == SET) { self->r.y = mp_obj_get_float(dest[1]); dest[0] = MP_OBJ_NULL; return; }
    }
    case MP_QSTR_w:
    {
      if (action == GET) { dest[0] = mp_obj_new_float(self->r.w); return; }
      if (action == SET) { self->r.w = mp_obj_get_float(dest[1]); dest[0] = MP_OBJ_NULL; return; }
    }
    case MP_QSTR_h:
    {
      if (action == GET) { dest[0] = mp_obj_new_float(self->r.h); return; }
      if (action == SET) { self->r.h = mp_obj_get_float(dest[1]); dest[0] = MP_OBJ_NULL; return; }
    }
    case MP_QSTR_r:
    {
      if (action == GET) { dest[0] = mp_obj_new_float(self->r.w + self->r.x); return; }
      if (action == SET) { self->r.w = mp_obj_get_float(dest[1]) - self->r.x; dest[0] = MP_OBJ_NULL; return; }
    }
    case MP_QSTR_b:
    {
      if (action == GET) { dest[0] = mp_obj_new_float(self->r.h + self->r.y); return; }
      if (action == SET) { self->r.h = mp_obj_get_float(dest[1]) - self->r.y; dest[0] = MP_OBJ_NULL; return; }
    }
  }
  dest[1] = MP_OBJ_SENTINEL;
}

static const mp_rom_map_elem_t rect_locals_dict_table[] = {
  { MP_ROM_QSTR(MP_QSTR_deflate), MP_ROM_PTR(&mpy_rect_deflate_obj) },
  { MP_ROM_QSTR(MP_QSTR_inflate), MP_ROM_PTR(&mpy_rect_inflate_obj) },
  { MP_ROM_QSTR(MP_QSTR_intersection), MP_ROM_PTR(&mpy_rect_intersection_obj) },
  { MP_ROM_QSTR(MP_QSTR_intersects), MP_ROM_PTR(&mpy_rect_intersects_obj) },
  { MP_ROM_QSTR(MP_QSTR_empty), MP_ROM_PTR(&mpy_rect_empty_obj) },
  { MP_ROM_QSTR(MP_QSTR_offset), MP_ROM_PTR(&mpy_rect_offset_obj) },
  { MP_ROM_QSTR(MP_QSTR_contains), MP_ROM_PTR(&mpy_rect_contains_obj) },
};
static MP_DEFINE_CONST_DICT(rect_locals_dict, rect_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
  type_rect,
  MP_QSTR_rect,
  MP_TYPE_FLAG_NONE,
  make_new, (const void *)rect_make_new,
  print, (const void *)rect_print,
  attr, (const void *)rect_attr,
  locals_dict, &rect_locals_dict
);

}
