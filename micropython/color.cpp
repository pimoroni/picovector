#include "mp_tracked_allocator.hpp"

#include "mp_helpers.hpp"
#include "picovector.hpp"

extern "C" {

  #include "py/runtime.h"

  MPY_BIND_STATICMETHOD_VAR(3, rgb, {
    int r = (int)mp_obj_get_float(args[0]);
    int g = (int)mp_obj_get_float(args[1]);
    int b = (int)mp_obj_get_float(args[2]);
    int a = n_args > 3 ? (int)mp_obj_get_float(args[3]) : 255;
    color_obj_t *color = mp_obj_malloc(color_obj_t, &type_color);
    color->c = _make_col(r, g, b, a);
    return MP_OBJ_FROM_PTR(color);
  })

  MPY_BIND_STATICMETHOD_VAR(3, hsv, {
    int h = (int)mp_obj_get_float(args[0]);
    int s = (int)mp_obj_get_float(args[1]);
    int v = (int)mp_obj_get_float(args[2]);
    int a = n_args > 3 ? (int)mp_obj_get_float(args[3]) : 255;
    color_obj_t *color = mp_obj_malloc(color_obj_t, &type_color);
    color->c = _make_col_hsv(h, s, v, a);
    return MP_OBJ_FROM_PTR(color);
  })

  MPY_BIND_STATICMETHOD_VAR(3, oklch, {
    int l = (int)mp_obj_get_float(args[0]);
    int c = (int)mp_obj_get_float(args[1]);
    int h = (int)mp_obj_get_float(args[2]);
    int a = n_args > 3 ? (int)mp_obj_get_float(args[3]) : 255;
    color_obj_t *color = mp_obj_malloc(color_obj_t, &type_color);
    color->c = _make_col_oklch(l, c, h, a);
    return MP_OBJ_FROM_PTR(color);
  })

  MPY_BIND_VAR(2, blend, {
    const color_obj_t *self = (color_obj_t *)MP_OBJ_TO_PTR(args[0]);
    const color_obj_t *other = (color_obj_t *)MP_OBJ_TO_PTR(args[1]);
    _blend_rgba_rgba((uint8_t*)&self->c, (uint8_t*)&other->c);
    return MP_OBJ_NULL;
  })

  static inline uint8_t darken_u8(uint8_t c, uint8_t factor) {
    return (uint8_t)((c * factor) >> 8);
  }

  MPY_BIND_VAR(2, darken, {
    const color_obj_t *self = (color_obj_t *)MP_OBJ_TO_PTR(args[0]);
    int v = 255 - (int)mp_obj_get_float(args[1]);
    _r(&self->c, darken_u8(_r(&self->c), v));
    _g(&self->c, darken_u8(_g(&self->c), v));
    _b(&self->c, darken_u8(_b(&self->c), v));
    return MP_OBJ_NULL;
  })

  static inline uint8_t lighten_u8(uint8_t c, uint factor) {
    // factor >= 256 (1.0x)
    uint16_t v = (c * factor) >> 8;
    return v > 255 ? 255 : (uint8_t)v;
  }

  MPY_BIND_VAR(2, lighten, {
    const color_obj_t *self = (color_obj_t *)MP_OBJ_TO_PTR(args[0]);
    int v = 256 + (int)mp_obj_get_float(args[1]);
    _r(&self->c, lighten_u8(_r(&self->c), v));
    _g(&self->c, lighten_u8(_g(&self->c), v));
    _b(&self->c, lighten_u8(_b(&self->c), v));
    return MP_OBJ_NULL;
  })

  static void attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    self(self_in, color_obj_t);

    action_t action = m_attr_action(dest);

    constexpr size_t GET = 0b1 << 31;
    constexpr size_t SET = 0b1 << 30;
    constexpr size_t DELETE = 0b1 << 29;

    switch(attr | action) {
      case MP_QSTR_r | GET:
        dest[0] = mp_obj_new_int(_r(&self->c));
        return;

      case MP_QSTR_r | SET:
        _r(&self->c, (int)mp_obj_get_float(dest[1]));
        dest[0] = MP_OBJ_NULL;
        return;

      case MP_QSTR_g | GET:
        dest[0] = mp_obj_new_int(_g(&self->c));
        return;

      case MP_QSTR_g | SET:
        _g(&self->c, (int)mp_obj_get_float(dest[1]));
        dest[0] = MP_OBJ_NULL;
        return;

      case MP_QSTR_b | GET:
        dest[0] = mp_obj_new_int(_b(&self->c));
        return;

      case MP_QSTR_b | SET:
        _b(&self->c, (int)mp_obj_get_float(dest[1]));
        dest[0] = MP_OBJ_NULL;
        return;

      case MP_QSTR_a | GET:
        dest[0] = mp_obj_new_int(_a(&self->c));
        return;

      case MP_QSTR_a | SET:
        _a(&self->c, (int)mp_obj_get_float(dest[1]));
        dest[0] = MP_OBJ_NULL;
        return;

      case MP_QSTR_raw | GET:
        dest[0] = mp_obj_new_int(self->c);
        return;

      case MP_QSTR_raw | SET:
        self->c = mp_obj_get_int(dest[1]);
        dest[0] = MP_OBJ_NULL;
        return;
    };

    dest[1] = MP_OBJ_SENTINEL;
  }

  MPY_BIND_LOCALS_DICT(color,
    MPY_BIND_ROM_PTR_STATIC(rgb),
    MPY_BIND_ROM_PTR_STATIC(hsv),
    MPY_BIND_ROM_PTR_STATIC(oklch),
    MPY_BIND_ROM_PTR(darken),
    MPY_BIND_ROM_PTR(lighten),
    MPY_BIND_ROM_PTR(blend),
  )

  MP_DEFINE_CONST_OBJ_TYPE(
      type_color,
      MP_QSTR_color,
      MP_TYPE_FLAG_NONE,
      attr, (const void *)attr,
      locals_dict, &color_locals_dict
  );

}


