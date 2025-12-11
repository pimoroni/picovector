#include "mp_tracked_allocator.hpp"

#include "mp_helpers.hpp"
#include "picovector.hpp"

using namespace picovector;

extern "C" {

  #include "py/runtime.h"


  MPY_BIND_NEW(point, {
    point_obj_t *self = mp_obj_malloc_with_finaliser(point_obj_t, type);
    if(n_args != 2) {
      mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("point constructor takes two values: x, y"));
    }
    self->point.x = mp_obj_get_float(args[0]);
    self->point.y = mp_obj_get_float(args[1]);
    return MP_OBJ_FROM_PTR(self);
  })

  static void attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    self(self_in, point_obj_t);

    action_t action = m_attr_action(dest);

    switch(attr | action) {
      case MP_QSTR_x | GET:
        dest[0] = mp_obj_new_int(self->point.x);
        return;

      case MP_QSTR_x | SET:
        self->point.x = mp_obj_get_int(dest[1]);
        dest[0] = MP_OBJ_NULL;
        return;

      case MP_QSTR_y | GET:
        dest[0] = mp_obj_new_int(self->point.y);
        return;

      case MP_QSTR_y | SET:
        self->point.y = mp_obj_get_int(dest[1]);
        dest[0] = MP_OBJ_NULL;
        return;
    };

    dest[1] = MP_OBJ_SENTINEL;
  }

  MPY_BIND_LOCALS_DICT(point,
  )

  MP_DEFINE_CONST_OBJ_TYPE(
      type_point,
      MP_QSTR_point,
      MP_TYPE_FLAG_NONE,
      make_new, (const void *)point_new,
      attr, (const void *)attr,
      locals_dict, &point_locals_dict
  );

}


