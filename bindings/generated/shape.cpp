// AUTO-GENERATED from api/shape.py by generate.py — do not edit by hand.
#include "pv_bindings.hpp"
#include "types.h"

extern "C" {

// shape.custom: Custom shape from one or more lists of vec2 points (extra lists = holes).
mp_obj_t mpy_shape_custom(size_t n_args, const mp_obj_t *args) {
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_shape_custom);
#endif
  shape_t *paths_shape = new (PV_MALLOC(sizeof(shape_t))) shape_t(n_args);
  for (size_t _p = 0; _p < n_args; _p++) {
    if (!mp_obj_is_type(args[_p], &mp_type_list)) mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("expected a list of vec2 points"));
    size_t _pc; mp_obj_t *_pts; mp_obj_list_get(args[_p], &_pc, &_pts);
    path_t poly(_pc);
    for (size_t _k = 0; _k < _pc; _k++) {
      if (!mp_obj_is_type(_pts[_k], &type_vec2)) mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("expected a list of vec2 points"));
      poly.add_point(((vec2_obj_t *)MP_OBJ_TO_PTR(_pts[_k]))->v);
    }
    paths_shape->add_path(poly);
  }
  return pv::box_shape(paths_shape);
}

// shape.regular_polygon: Regular polygon. Args: centre, radius, number of sides.
mp_obj_t mpy_shape_regular_polygon(size_t n_args, const mp_obj_t *args) {
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_shape_regular_polygon);
#endif
  size_t _i = 0;
  vec2_t c = pv::get_xy(args, &_i, n_args);
  pv::need(n_args, _i + 2);
  float r = mp_obj_get_float(args[_i]); _i++;
  int sides = (int)mp_obj_get_float(args[_i]); _i++;
  return pv::box_shape(regular_polygon(c.x, c.y, sides, r));
}

// shape.circle: Circle. Args: centre (vec2 or x, y), radius.
mp_obj_t mpy_shape_circle(size_t n_args, const mp_obj_t *args) {
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_shape_circle);
#endif
  size_t _i = 0;
  vec2_t c = pv::get_xy(args, &_i, n_args);
  pv::need(n_args, _i + 1);
  float r = mp_obj_get_float(args[_i]); _i++;
  return pv::box_shape(circle(c.x, c.y, r));
}

// shape.ellipse: Ellipse. Args: centre, x-radius, y-radius.
mp_obj_t mpy_shape_ellipse(size_t n_args, const mp_obj_t *args) {
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_shape_ellipse);
#endif
  size_t _i = 0;
  vec2_t c = pv::get_xy(args, &_i, n_args);
  pv::need(n_args, _i + 2);
  float rx = mp_obj_get_float(args[_i]); _i++;
  float ry = mp_obj_get_float(args[_i]); _i++;
  return pv::box_shape(ellipse(c.x, c.y, rx, ry));
}

// shape.rectangle: Rectangle. Args: a rect, or x, y, w, h.
mp_obj_t mpy_shape_rectangle(size_t n_args, const mp_obj_t *args) {
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_shape_rectangle);
#endif
  size_t _i = 0;
  rect_t r = pv::get_xywh(args, &_i, n_args);
  return pv::box_shape(rectangle(r.x, r.y, r.w, r.h));
}

// shape.rounded_rectangle: Rounded rectangle. Corner radii (TL, TR, BR, BL) default to r1.
mp_obj_t mpy_shape_rounded_rectangle(size_t n_args, const mp_obj_t *args) {
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_shape_rounded_rectangle);
#endif
  size_t _i = 0;
  rect_t r = pv::get_xywh(args, &_i, n_args);
  pv::need(n_args, _i + 1);
  float r1 = mp_obj_get_float(args[_i]); _i++;
  float r2 = r1;
  if (n_args > _i) { r2 = mp_obj_get_float(args[_i]); _i++; }
  float r3 = r1;
  if (n_args > _i) { r3 = mp_obj_get_float(args[_i]); _i++; }
  float r4 = r1;
  if (n_args > _i) { r4 = mp_obj_get_float(args[_i]); _i++; }
  return pv::box_shape(rounded_rectangle(r.x, r.y, r.w, r.h, r1, r2, r3, r4));
}

// shape.squircle: Squircle (super-ellipse). Args: centre, size; n is the exponent (default 4).
mp_obj_t mpy_shape_squircle(size_t n_args, const mp_obj_t *args) {
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_shape_squircle);
#endif
  size_t _i = 0;
  vec2_t c = pv::get_xy(args, &_i, n_args);
  pv::need(n_args, _i + 1);
  float s = mp_obj_get_float(args[_i]); _i++;
  float n = 4.0;
  if (n_args > _i) { n = mp_obj_get_float(args[_i]); _i++; }
  if (n < 2) n = 2;
  return pv::box_shape(squircle(c.x, c.y, s, n));
}

// shape.arc: Annular sector / arc. Args: centre, inner & outer radius, from/to angle (degrees).
mp_obj_t mpy_shape_arc(size_t n_args, const mp_obj_t *args) {
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_shape_arc);
#endif
  size_t _i = 0;
  vec2_t c = pv::get_xy(args, &_i, n_args);
  pv::need(n_args, _i + 4);
  float inner = mp_obj_get_float(args[_i]); _i++;
  float outer = mp_obj_get_float(args[_i]); _i++;
  float from_a = mp_obj_get_float(args[_i]); _i++;
  float to_a = mp_obj_get_float(args[_i]); _i++;
  return pv::box_shape(arc(c.x, c.y, from_a, to_a, inner, outer));
}

// shape.pie: Pie / sector slice. Args: centre, radius, from/to angle (degrees).
mp_obj_t mpy_shape_pie(size_t n_args, const mp_obj_t *args) {
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_shape_pie);
#endif
  size_t _i = 0;
  vec2_t c = pv::get_xy(args, &_i, n_args);
  pv::need(n_args, _i + 3);
  float r = mp_obj_get_float(args[_i]); _i++;
  float from_a = mp_obj_get_float(args[_i]); _i++;
  float to_a = mp_obj_get_float(args[_i]); _i++;
  return pv::box_shape(pie(c.x, c.y, from_a, to_a, r));
}

// shape.star: Star polygon. Args: centre, point count, outer & inner radius.
mp_obj_t mpy_shape_star(size_t n_args, const mp_obj_t *args) {
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_shape_star);
#endif
  size_t _i = 0;
  vec2_t c = pv::get_xy(args, &_i, n_args);
  pv::need(n_args, _i + 3);
  int points = (int)mp_obj_get_float(args[_i]); _i++;
  float outer = mp_obj_get_float(args[_i]); _i++;
  float inner = mp_obj_get_float(args[_i]); _i++;
  return pv::box_shape(star(c.x, c.y, points, outer, inner));
}

// shape.line: Stroked line shape of width w between two points.
mp_obj_t mpy_shape_line(size_t n_args, const mp_obj_t *args) {
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_shape_line);
#endif
  size_t _i = 0;
  vec2_t p1 = pv::get_xy(args, &_i, n_args);
  vec2_t p2 = pv::get_xy(args, &_i, n_args);
  pv::need(n_args, _i + 1);
  float w = mp_obj_get_float(args[_i]); _i++;
  return pv::box_shape(line(p1.x, p1.y, p2.x, p2.y, w));
}

// shape.stroke: Replace this shape with its stroked outline. flags: OR of
mp_obj_t mpy_shape_stroke(size_t n_args, const mp_obj_t *args) {
  self(args[0], shape_obj_t);
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_shape_stroke);
#endif
  size_t _i = 1;
  float width = mp_obj_get_float(args[_i]); _i++;
  int flags = 0;
  if (n_args > _i) { flags = (int)mp_obj_get_float(args[_i]); _i++; }
  float miter_limit = 4.0;
  if (n_args > _i) { miter_limit = mp_obj_get_float(args[_i]); _i++; }
  self->shape->stroke(width, flags, miter_limit);
  return MP_OBJ_FROM_PTR(self);
}

// shape.bounds: Device-space bounding box (local bbox run through the current transform).
mp_obj_t mpy_shape_bounds(size_t n_args, const mp_obj_t *args) {
  self(args[0], shape_obj_t);
#if PV_METRICS
  pv::metric_scope _pvm(PV_M_shape_bounds);
#endif
  return pv::box_rect(self->shape->bounds());
}

static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_shape_custom_obj, 1, mpy_shape_custom);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_shape_custom_static_obj, MP_ROM_PTR(&mpy_shape_custom_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_shape_regular_polygon_obj, 3, mpy_shape_regular_polygon);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_shape_regular_polygon_static_obj, MP_ROM_PTR(&mpy_shape_regular_polygon_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_shape_circle_obj, 2, mpy_shape_circle);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_shape_circle_static_obj, MP_ROM_PTR(&mpy_shape_circle_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_shape_ellipse_obj, 3, mpy_shape_ellipse);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_shape_ellipse_static_obj, MP_ROM_PTR(&mpy_shape_ellipse_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_shape_rectangle_obj, 1, mpy_shape_rectangle);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_shape_rectangle_static_obj, MP_ROM_PTR(&mpy_shape_rectangle_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_shape_rounded_rectangle_obj, 2, mpy_shape_rounded_rectangle);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_shape_rounded_rectangle_static_obj, MP_ROM_PTR(&mpy_shape_rounded_rectangle_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_shape_squircle_obj, 2, mpy_shape_squircle);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_shape_squircle_static_obj, MP_ROM_PTR(&mpy_shape_squircle_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_shape_arc_obj, 5, mpy_shape_arc);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_shape_arc_static_obj, MP_ROM_PTR(&mpy_shape_arc_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_shape_pie_obj, 4, mpy_shape_pie);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_shape_pie_static_obj, MP_ROM_PTR(&mpy_shape_pie_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_shape_star_obj, 4, mpy_shape_star);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_shape_star_static_obj, MP_ROM_PTR(&mpy_shape_star_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_shape_line_obj, 3, mpy_shape_line);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(mpy_shape_line_static_obj, MP_ROM_PTR(&mpy_shape_line_obj));
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_shape_stroke_obj, 2, mpy_shape_stroke);
static MP_DEFINE_CONST_FUN_OBJ_VAR(mpy_shape_bounds_obj, 1, mpy_shape_bounds);

static mp_obj_t shape__del__(mp_obj_t self_in) {
  self(self_in, shape_obj_t);
  m_del_class(shape_t, self->shape);
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(shape__del___obj, shape__del__);

void shape_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
  self(self_in, shape_obj_t);
  action_t action = m_attr_action(dest);
  switch (attr) {
    case MP_QSTR_transform:
    {
      if (action == GET) { dest[0] = pv::box_mat3(self->shape->transform); return; }
      if (action == SET) { self->shape->transform = ((mat3_obj_t *)MP_OBJ_TO_PTR(dest[1]))->m; dest[0] = MP_OBJ_NULL; return; }
    }
  }
  dest[1] = MP_OBJ_SENTINEL;
}

static const mp_rom_map_elem_t shape_locals_dict_table[] = {
  { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&shape__del___obj) },
  { MP_ROM_QSTR(MP_QSTR_ALIGN_OUTER), MP_ROM_INT(ALIGN_OUTER) },
  { MP_ROM_QSTR(MP_QSTR_ALIGN_INNER), MP_ROM_INT(ALIGN_INNER) },
  { MP_ROM_QSTR(MP_QSTR_ALIGN_CENTER), MP_ROM_INT(ALIGN_CENTER) },
  { MP_ROM_QSTR(MP_QSTR_PATH_CLOSED), MP_ROM_INT(PATH_CLOSED) },
  { MP_ROM_QSTR(MP_QSTR_PATH_OPEN), MP_ROM_INT(PATH_OPEN) },
  { MP_ROM_QSTR(MP_QSTR_JOIN_MITER), MP_ROM_INT(JOIN_MITER) },
  { MP_ROM_QSTR(MP_QSTR_JOIN_ROUND), MP_ROM_INT(JOIN_ROUND) },
  { MP_ROM_QSTR(MP_QSTR_JOIN_BEVEL), MP_ROM_INT(JOIN_BEVEL) },
  { MP_ROM_QSTR(MP_QSTR_CAP_BUTT), MP_ROM_INT(CAP_BUTT) },
  { MP_ROM_QSTR(MP_QSTR_CAP_ROUND), MP_ROM_INT(CAP_ROUND) },
  { MP_ROM_QSTR(MP_QSTR_CAP_SQUARE), MP_ROM_INT(CAP_SQUARE) },
  { MP_ROM_QSTR(MP_QSTR_custom), MP_ROM_PTR(&mpy_shape_custom_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_regular_polygon), MP_ROM_PTR(&mpy_shape_regular_polygon_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_circle), MP_ROM_PTR(&mpy_shape_circle_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_ellipse), MP_ROM_PTR(&mpy_shape_ellipse_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_rectangle), MP_ROM_PTR(&mpy_shape_rectangle_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_rounded_rectangle), MP_ROM_PTR(&mpy_shape_rounded_rectangle_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_squircle), MP_ROM_PTR(&mpy_shape_squircle_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_arc), MP_ROM_PTR(&mpy_shape_arc_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_pie), MP_ROM_PTR(&mpy_shape_pie_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_star), MP_ROM_PTR(&mpy_shape_star_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_line), MP_ROM_PTR(&mpy_shape_line_static_obj) },
  { MP_ROM_QSTR(MP_QSTR_stroke), MP_ROM_PTR(&mpy_shape_stroke_obj) },
  { MP_ROM_QSTR(MP_QSTR_bounds), MP_ROM_PTR(&mpy_shape_bounds_obj) },
};
static MP_DEFINE_CONST_DICT(shape_locals_dict, shape_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
  type_shape,
  MP_QSTR_shape,
  MP_TYPE_FLAG_NONE,
  attr, (const void *)shape_attr,
  locals_dict, &shape_locals_dict
);

}
