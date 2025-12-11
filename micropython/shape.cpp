#include "mp_helpers.hpp"
#include "picovector.hpp"

extern "C" {

  #include "py/runtime.h"
  extern image_obj_t *default_target;

  mp_obj_t shape__del__(mp_obj_t self_in) {
    self(self_in, shape_obj_t);
    m_del_class(shape_t, self->shape);
    return mp_const_none;
  }
  static MP_DEFINE_CONST_FUN_OBJ_1(shape__del___obj, shape__del__);

  MPY_BIND_STATICMETHOD_VAR(1, custom, {
    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = new(PV_MALLOC(sizeof(shape_t))) shape_t(1);

    for (size_t arg_i = 0; arg_i < n_args; arg_i++) {
      mp_obj_t path_obj = args[arg_i];

      if(!mp_obj_is_type(path_obj, &mp_type_list)) {
        mp_raise_TypeError(MP_ERROR_TEXT("expected a list of tuples"));
      }

      size_t count;
      mp_obj_t *path;
      mp_obj_list_get(path_obj, &count, &path);

      path_t poly(count);

      for(size_t i = 0; i < count; i++) {
        mp_obj_t point_obj = path[i];

        if(!mp_obj_is_type(point_obj, &mp_type_tuple)) {
          mp_raise_TypeError(MP_ERROR_TEXT("list elements must be tuples"));
        }

        size_t tuple_len;
        mp_obj_t *tuple_items;
        mp_obj_tuple_get(point_obj, &tuple_len, &tuple_items);

        if (tuple_len != 2) {
          mp_raise_ValueError(MP_ERROR_TEXT("tuples must contain (x, y) coordinates"));
        }

        // Extract elements; assuming ints here
        float x = mp_obj_get_float(tuple_items[0]);
        float y = mp_obj_get_float(tuple_items[1]);

        poly.add_point(x, y);
      }

      shape->shape->add_path(poly);
    }

    return MP_OBJ_FROM_PTR(shape);
  })

  MPY_BIND_STATICMETHOD_VAR(4, regular_polygon, {
    float x = mp_obj_get_float(args[0]);
    float y = mp_obj_get_float(args[1]);
    float r = mp_obj_get_float(args[2]);
    int s = mp_obj_get_float(args[3]);
    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = regular_polygon(x, y, s, r);
    return MP_OBJ_FROM_PTR(shape);
  })

  MPY_BIND_STATICMETHOD_VAR(3, circle, {
    float x = mp_obj_get_float(args[0]);
    float y = mp_obj_get_float(args[1]);
    float r = mp_obj_get_float(args[2]);
    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = circle(x, y, r);
    return MP_OBJ_FROM_PTR(shape);
  })

  MPY_BIND_STATICMETHOD_VAR(4, rectangle, {
    float x = mp_obj_get_float(args[0]);
    float y = mp_obj_get_float(args[1]);
    float w = mp_obj_get_float(args[2]);
    float h = mp_obj_get_float(args[3]);
    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = rectangle(x, y, w, h);
    return MP_OBJ_FROM_PTR(shape);
  })

  MPY_BIND_STATICMETHOD_VAR(5, rounded_rectangle, {
    float x = mp_obj_get_float(args[0]);
    float y = mp_obj_get_float(args[1]);
    float w = mp_obj_get_float(args[2]);
    float h = mp_obj_get_float(args[3]);
    float r1 = mp_obj_get_float(args[4]);
    float r2 = r1;
    float r3 = r1;
    float r4 = r1;
    if(n_args >= 6) { r2 = mp_obj_get_float(args[5]); }
    if(n_args >= 7) { r3 = mp_obj_get_float(args[6]); }
    if(n_args >= 8) { r4 = mp_obj_get_float(args[7]); }

    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = rounded_rectangle(x, y, w, h, r1, r2, r3, r4);
    return MP_OBJ_FROM_PTR(shape);
  })

  MPY_BIND_STATICMETHOD_VAR(3, squircle, {
    float x = mp_obj_get_float(args[0]);
    float y = mp_obj_get_float(args[1]);
    float s = mp_obj_get_float(args[2]);
    float n = 4.0f;
    if(n_args == 4) {
      n = mp_obj_get_float(args[3]);
      n = max(2.0f, n);
      n = max(2.0f, n);
    }
    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = squircle(x, y, s, n);
    return MP_OBJ_FROM_PTR(shape);
  })

  MPY_BIND_STATICMETHOD_VAR(4, arc, {
    float x = mp_obj_get_float(args[0]);
    float y = mp_obj_get_float(args[1]);
    float r = mp_obj_get_float(args[2]);
    float f = mp_obj_get_float(args[3]);
    float t = mp_obj_get_float(args[4]);
    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = arc(x, y, f, t, r);
    return MP_OBJ_FROM_PTR(shape);
  })

  MPY_BIND_STATICMETHOD_VAR(4, pie, {
    float x = mp_obj_get_float(args[0]);
    float y = mp_obj_get_float(args[1]);
    float r = mp_obj_get_float(args[2]);
    float f = mp_obj_get_float(args[3]);
    float t = mp_obj_get_float(args[4]);
    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = pie(x, y, f, t, r);
    return MP_OBJ_FROM_PTR(shape);
  })

  MPY_BIND_STATICMETHOD_VAR(4, star, {
    float x = mp_obj_get_float(args[0]);
    float y = mp_obj_get_float(args[1]);
    int s = mp_obj_get_float(args[2]);
    float ro = mp_obj_get_float(args[3]);
    float ri = mp_obj_get_float(args[4]);
    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = star(x, y, s, ro, ri);
    return MP_OBJ_FROM_PTR(shape);
  })

  MPY_BIND_STATICMETHOD_VAR(5, line, {
    float x1 = mp_obj_get_float(args[0]);
    float y1 = mp_obj_get_float(args[1]);
    float x2 = mp_obj_get_float(args[2]);
    float y2 = mp_obj_get_float(args[3]);
    float w = mp_obj_get_float(args[4]);
    shape_obj_t *shape = mp_obj_malloc_with_finaliser(shape_obj_t, &type_Shape);
    shape->shape = line(x1, y1, x2, y2, w);
    return MP_OBJ_FROM_PTR(shape);
  })

  MPY_BIND_VAR(1, stroke, {
    const shape_obj_t *self = (shape_obj_t *)MP_OBJ_TO_PTR(args[0]);
    float width = mp_obj_get_float(args[1]);
    self->shape->stroke(width);
    return MP_OBJ_FROM_PTR(self);
  })

  static void shape_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    self(self_in, shape_obj_t);

    action_t action = m_attr_action(dest);

    switch(attr) {
      case MP_QSTR_transform: {
        if(action == GET) {
          matrix_obj_t *out = mp_obj_malloc_with_finaliser(matrix_obj_t, &type_Matrix);
          out->m = self->shape->transform;
          dest[0] = MP_OBJ_FROM_PTR(out);
          return;
        }

        if(action == SET) {
          if(!mp_obj_is_type(dest[1], &type_Matrix)) {
            mp_raise_TypeError(MP_ERROR_TEXT("expected Matrix"));
          }
          matrix_obj_t *in = (matrix_obj_t *)MP_OBJ_TO_PTR(dest[1]);
          self->shape->transform = in->m;
          dest[0] = MP_OBJ_NULL;
          return;
        }
      };

      // case MP_QSTR_antialias: {
      //   if(action == GET) {
      //     dest[0] = mp_obj_new_int(self->image->antialias());
      //     return;
      //   }

      //   if(action == SET) {
      //     self->image->antialias((antialias_t)mp_obj_get_int(dest[1]));
      //     dest[0] = MP_OBJ_NULL;
      //     return;
      //   }
      // };

      case MP_QSTR_brush: {
        if(action == GET) {
          if(self->brush) {
            dest[0] = MP_OBJ_FROM_PTR(self->brush);
          }else{
            dest[0] = mp_const_none;
          }
          return;
        }

        if(action == SET) {
          if(!mp_obj_is_type(dest[1], &type_brush)) {
            mp_raise_TypeError(MP_ERROR_TEXT("value must be of type Brush"));
          }
          brush_obj_t *brush = (brush_obj_t *)dest[1];
          self->shape->brush(brush->brush);
          dest[0] = MP_OBJ_NULL;
          return;
        }
      };
    }

    dest[1] = MP_OBJ_SENTINEL;
  }

  mp_obj_t shape_draw(size_t n_args, const mp_obj_t *args) {
    const shape_obj_t *self = (shape_obj_t *)MP_OBJ_TO_PTR(args[0]);

    if(n_args == 2) {
      const image_obj_t *image = (image_obj_t *)MP_OBJ_TO_PTR(args[1]);
      image->image->draw(self->shape);
    }else{
      default_target->image->draw(self->shape);
    }



    // brush = red


    // frectangle(10, 10, 20, 20)
    // frectanglefill(10, 10, 20, 20)
    // fcircle(20, 20, 30)
    // fcirclefill(20, 20, 30)
    // fset(10, 10, (40, 50, 60))
    // fline(10, 10, 20, 20)



    // rect = vector.rectangle(10, 10, 20, 20) // save for later
    // rect.brush = green
    // vector.rectangle(10, 10, 20, 20).draw() // draw now


    // rect.draw()




    // rect = shapes.rectangle(10, 10, 20, 20, brush=None, antialias=X4)
    // rect.brush = red
    // rect.draw()


    return mp_const_none;
  }
  static MP_DEFINE_CONST_FUN_OBJ_VAR(shape_draw_obj, 1, shape_draw);

  /*
    micropython bindings
  */

  MPY_BIND_LOCALS_DICT(shape,
      { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&shape__del___obj) },
      MPY_BIND_ROM_PTR(stroke),
      { MP_ROM_QSTR(MP_QSTR_draw), MP_ROM_PTR(&shape_draw_obj) },
  )

  MP_DEFINE_CONST_OBJ_TYPE(
      type_Shape,
      MP_QSTR_Shape,
      MP_TYPE_FLAG_NONE,
      attr, (const void *)shape_attr,
      locals_dict, &shape_locals_dict
  );

  MPY_BIND_LOCALS_DICT(shapes,
    MPY_BIND_ROM_PTR_STATIC(custom),
    MPY_BIND_ROM_PTR_STATIC(regular_polygon),
    MPY_BIND_ROM_PTR_STATIC(squircle),
    MPY_BIND_ROM_PTR_STATIC(circle),
    MPY_BIND_ROM_PTR_STATIC(rectangle),
    MPY_BIND_ROM_PTR_STATIC(rounded_rectangle),
    MPY_BIND_ROM_PTR_STATIC(arc),
    MPY_BIND_ROM_PTR_STATIC(pie),
    MPY_BIND_ROM_PTR_STATIC(star),
    MPY_BIND_ROM_PTR_STATIC(line),
  )

  MP_DEFINE_CONST_OBJ_TYPE(
      type_Shapes,
      MP_QSTR_shapes,
      MP_TYPE_FLAG_NONE,
      locals_dict, &shapes_locals_dict
  );


}
