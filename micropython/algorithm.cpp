#include "mp_helpers.hpp"
#include "picovector.hpp"

#include "../algorithms/algorithms.hpp"

extern "C" {
  #include "py/runtime.h"

  MPY_BIND_STATICMETHOD_VAR(3, clip_line, {
    vec2_obj_t *p1 = (vec2_obj_t *)MP_OBJ_TO_PTR(args[0]);
    vec2_obj_t *p2 = (vec2_obj_t *)MP_OBJ_TO_PTR(args[1]);
    const rect_obj_t *r = (rect_obj_t *)MP_OBJ_TO_PTR(args[2]);

    bool result = clip_line(p1->v, p2->v, r->r);
    return mp_obj_new_bool(result);
  })

 MPY_BIND_STATICMETHOD_VAR(3, dda, {
    vec2_obj_t *p = (vec2_obj_t *)MP_OBJ_TO_PTR(args[0]);
    vec2_obj_t *v = (vec2_obj_t *)MP_OBJ_TO_PTR(args[1]);
    //int max = mp_obj_get_int(args[2]);

    //mp_obj_t result = mp_obj_new_list(max, NULL);
    int i = 0;

    mp_obj_t cb_args[6];
    vec2_obj_t *cb_p = mp_obj_malloc_with_finaliser(vec2_obj_t, &type_vec2);
    vec2_obj_t *cb_g = mp_obj_malloc_with_finaliser(vec2_obj_t, &type_vec2);


    mp_obj_t callback = args[2];
    if (callback != mp_const_none && !mp_obj_is_callable(callback)) {
      mp_raise_TypeError(MP_ERROR_TEXT("callback must be callable"));
    }

    dda(p->v, v->v, [&i, &cb_args, &cb_p, &cb_g, &callback](float hit_x, float hit_y, int gx, int gy, int edge, float offset, float distance) -> bool {
      cb_p->v.x = hit_x;
      cb_p->v.y = hit_y;

      cb_g->v.x = gx;
      cb_g->v.y = gy;

      cb_args[0] = mp_obj_new_int(i);
      cb_args[1] = MP_OBJ_FROM_PTR(cb_p);
      cb_args[2] = MP_OBJ_FROM_PTR(cb_g);
      cb_args[3] = mp_obj_new_int(edge);
      cb_args[4] = mp_obj_new_float(offset);
      cb_args[5] = mp_obj_new_float(distance);

      mp_obj_t result = mp_call_function_n_kw(callback, MP_ARRAY_SIZE(cb_args), 0, cb_args);

      i++;
      return result == mp_const_true;
    });

    return mp_const_none;
  })


  MPY_BIND_LOCALS_DICT(algorithm,
    MPY_BIND_ROM_PTR_STATIC(clip_line),
    MPY_BIND_ROM_PTR_STATIC(dda),
  )


  MP_DEFINE_CONST_OBJ_TYPE(
      type_algorithm,
      MP_QSTR_algorithm,
      MP_TYPE_FLAG_NONE,
      locals_dict, &algorithm_locals_dict
  );


}


