
#include "mp_tracked_allocator.hpp"
#include "../picovector.hpp"
#include "../primitive.hpp"
#include "../shape.hpp"
#include "../image.hpp"

#define self(self_in, T) T *self = (T *)MP_OBJ_TO_PTR(self_in)
#define m_new_class(cls, ...) new(m_new(cls, 1)) cls(__VA_ARGS__)
#define m_del_class(cls, ptr) ptr->~cls(); m_del(cls, ptr, 1)

using namespace picovector;

extern "C" {

  #include "py/runtime.h"

  extern const mp_obj_type_t type_Shape;
  extern const mp_obj_type_t type_Shapes;

  typedef struct _shape_obj_t {
    mp_obj_base_t base;
    shape *shape;
  } shape_obj_t;

  mp_obj_t shape__del__(mp_obj_t self_in) {
    self(self_in, shape_obj_t);
    m_del_class(shape, self->shape);
    return mp_const_none;
  }

  mp_obj_t shapes_regular_polygon(size_t n_args, const mp_obj_t *pos_args) {
    float x = mp_obj_get_float(pos_args[0]);    
    float y = mp_obj_get_float(pos_args[1]);    
    float r = mp_obj_get_float(pos_args[2]);    
    int s = mp_obj_get_float(pos_args[3]);    
    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = regular_polygon(x, y, s, r);
    return MP_OBJ_FROM_PTR(shape);
  }

  mp_obj_t shapes_circle(size_t n_args, const mp_obj_t *pos_args) {
    float x = mp_obj_get_float(pos_args[0]);    
    float y = mp_obj_get_float(pos_args[1]);    
    float r = mp_obj_get_float(pos_args[2]);    
    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = circle(x, y, r);
    return MP_OBJ_FROM_PTR(shape);
  }

  mp_obj_t shapes_rectangle(size_t n_args, const mp_obj_t *pos_args) {
    float x1 = mp_obj_get_float(pos_args[0]);    
    float y1 = mp_obj_get_float(pos_args[1]);    
    float x2 = mp_obj_get_float(pos_args[2]);    
    float y2 = mp_obj_get_float(pos_args[3]);    
    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = rectangle(x1, y1, x2, y2);
    return MP_OBJ_FROM_PTR(shape);
  }

  mp_obj_t shapes_squircle(size_t n_args, const mp_obj_t *pos_args) {
    float x = mp_obj_get_float(pos_args[0]);    
    float y = mp_obj_get_float(pos_args[1]);    
    float s = mp_obj_get_float(pos_args[2]);    
    float n = mp_obj_get_float(pos_args[3]);    
    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = squircle(x, y, s, n);
    return MP_OBJ_FROM_PTR(shape);
  }

  mp_obj_t shapes_arc(size_t n_args, const mp_obj_t *pos_args) {
    float x = mp_obj_get_float(pos_args[0]);    
    float y = mp_obj_get_float(pos_args[1]);    
    float r = mp_obj_get_float(pos_args[2]);    
    float f = mp_obj_get_float(pos_args[3]);    
    float t = mp_obj_get_float(pos_args[4]);    
    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = arc(x, y, f, t, r);
    return MP_OBJ_FROM_PTR(shape);
  }

  mp_obj_t shapes_pie(size_t n_args, const mp_obj_t *pos_args) {
    float x = mp_obj_get_float(pos_args[0]);    
    float y = mp_obj_get_float(pos_args[1]);    
    float r = mp_obj_get_float(pos_args[2]);    
    float f = mp_obj_get_float(pos_args[3]);    
    float t = mp_obj_get_float(pos_args[4]);    
    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = pie(x, y, f, t, r);
    return MP_OBJ_FROM_PTR(shape);
  }

  mp_obj_t shapes_star(size_t n_args, const mp_obj_t *pos_args) {
    float x = mp_obj_get_float(pos_args[0]);    
    float y = mp_obj_get_float(pos_args[1]);    
    int s = mp_obj_get_float(pos_args[2]);    
    float ro = mp_obj_get_float(pos_args[3]);    
    float ri = mp_obj_get_float(pos_args[4]);    
    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = star(x, y, s, ro, ri);
    return MP_OBJ_FROM_PTR(shape);
  }

  mp_obj_t shapes_line(size_t n_args, const mp_obj_t *pos_args) {
    float x1 = mp_obj_get_float(pos_args[0]);    
    float y1 = mp_obj_get_float(pos_args[1]);    
    float x2 = mp_obj_get_float(pos_args[2]);    
    float y2 = mp_obj_get_float(pos_args[3]);    
    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = line(x1, y1, x2, y2);
    return MP_OBJ_FROM_PTR(shape);
  }

  /*
    micropython bindings
  */
  static MP_DEFINE_CONST_FUN_OBJ_1(shape__del___obj, shape__del__);

  static const mp_rom_map_elem_t shape_locals_dict_table[] = {
      { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&shape__del___obj) },
  };
  static MP_DEFINE_CONST_DICT(shape_locals_dict, shape_locals_dict_table);

  MP_DEFINE_CONST_OBJ_TYPE(
      type_Shape,
      MP_QSTR_Shape,
      MP_TYPE_FLAG_NONE,
      locals_dict, &shape_locals_dict
  );

  static MP_DEFINE_CONST_FUN_OBJ_VAR(shapes_regular_polygon_obj, 4, shapes_regular_polygon);
  static MP_DEFINE_CONST_STATICMETHOD_OBJ(shapes_regular_polygon_static_obj, MP_ROM_PTR(&shapes_regular_polygon_obj));

  static MP_DEFINE_CONST_FUN_OBJ_VAR(shapes_rectangle_obj, 4, shapes_rectangle);
  static MP_DEFINE_CONST_STATICMETHOD_OBJ(shapes_rectangle_static_obj, MP_ROM_PTR(&shapes_rectangle_obj));

  static MP_DEFINE_CONST_FUN_OBJ_VAR(shapes_squircle_obj, 4, shapes_squircle);
  static MP_DEFINE_CONST_STATICMETHOD_OBJ(shapes_squircle_static_obj, MP_ROM_PTR(&shapes_squircle_obj));

  static MP_DEFINE_CONST_FUN_OBJ_VAR(shapes_circle_obj, 4, shapes_circle);
  static MP_DEFINE_CONST_STATICMETHOD_OBJ(shapes_circle_static_obj, MP_ROM_PTR(&shapes_circle_obj));

  static MP_DEFINE_CONST_FUN_OBJ_VAR(shapes_arc_obj, 4, shapes_arc);
  static MP_DEFINE_CONST_STATICMETHOD_OBJ(shapes_arc_static_obj, MP_ROM_PTR(&shapes_arc_obj));

  static MP_DEFINE_CONST_FUN_OBJ_VAR(shapes_pie_obj, 4, shapes_pie);
  static MP_DEFINE_CONST_STATICMETHOD_OBJ(shapes_pie_static_obj, MP_ROM_PTR(&shapes_pie_obj));

  static MP_DEFINE_CONST_FUN_OBJ_VAR(shapes_star_obj, 4, shapes_star);
  static MP_DEFINE_CONST_STATICMETHOD_OBJ(shapes_star_static_obj, MP_ROM_PTR(&shapes_star_obj));

  static MP_DEFINE_CONST_FUN_OBJ_VAR(shapes_line_obj, 4, shapes_line);
  static MP_DEFINE_CONST_STATICMETHOD_OBJ(shapes_line_static_obj, MP_ROM_PTR(&shapes_line_obj));

  static const mp_rom_map_elem_t shapes_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_regular_polygon), MP_ROM_PTR(&shapes_regular_polygon_static_obj) },
    { MP_ROM_QSTR(MP_QSTR_squircle), MP_ROM_PTR(&shapes_squircle_static_obj) },
    { MP_ROM_QSTR(MP_QSTR_circle), MP_ROM_PTR(&shapes_circle_static_obj) },
    { MP_ROM_QSTR(MP_QSTR_rectangle), MP_ROM_PTR(&shapes_rectangle_static_obj) },
    { MP_ROM_QSTR(MP_QSTR_arc), MP_ROM_PTR(&shapes_arc_static_obj) },
    { MP_ROM_QSTR(MP_QSTR_pie), MP_ROM_PTR(&shapes_pie_static_obj) },
    { MP_ROM_QSTR(MP_QSTR_pie), MP_ROM_PTR(&shapes_star_static_obj) },
    { MP_ROM_QSTR(MP_QSTR_line), MP_ROM_PTR(&shapes_line_static_obj) },

  };
  static MP_DEFINE_CONST_DICT(shapes_locals_dict, shapes_locals_dict_table);

  MP_DEFINE_CONST_OBJ_TYPE(
      type_Shapes,
      MP_QSTR_shapes,
      MP_TYPE_FLAG_NONE,
      locals_dict, &shapes_locals_dict
  );


}
