#include "mp_tracked_allocator.hpp"
// #include "../picovector.hpp"
// #include "../primitive.hpp"
// #include "../image.hpp"


#include "brush.hpp"
#include "shape.hpp"
#include "font.hpp"
#include "image.hpp"
#include "input.hpp"

#define self(self_in, T) T *self = (T *)MP_OBJ_TO_PTR(self_in)

using namespace picovector;

extern "C" {

  #include "py/runtime.h"

  extern const mp_obj_type_t type_PicoVector;

  typedef struct _modpicovector_obj_t {
      mp_obj_base_t base;
  } modpicovector_obj_t;

  input_obj_t *mp_input;
  image_obj_t *mp_image;
  int screen_width = 160;
  int screen_height = 120;
  uint32_t framebuffer[160 * 120];
  image screen(framebuffer, screen_width, screen_height);

  extern const mp_rom_map_elem_t modpicovector_globals_table;

  void modpicovector_deinit() {
    debug_printf("modpicovector_deinit: Cleaning up...\n");
    mp_image = nullptr;
    mp_input = nullptr;
  }

  void modpicovector_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    if (dest[0] == MP_OBJ_NULL) {
      if (attr == MP_QSTR_screen) {
        if(!mp_image) {
          // Don't use mp_obj_malloc_with_finalizer here, since the __del__
          // method will try to delete our `screen` and explode.
          mp_image = mp_obj_malloc(image_obj_t, &type_Image);
          mp_image->image = &screen;
        }
        dest[0] = MP_OBJ_FROM_PTR(mp_image);
      }
      if (attr == MP_QSTR_io) {
        if(!mp_input) {
          mp_input = mp_obj_malloc(input_obj_t, &type_Input);
        }
        dest[0] = MP_OBJ_FROM_PTR(mp_input);
      }
    }
  }

  mp_obj_t modpicovector_brush(mp_obj_t brush_in) {
      if(!mp_obj_is_exact_type(brush_in, &type_Brush)) {
          mp_raise_ValueError(MP_ERROR_TEXT("brush: Must be a valid brush!"));
      }
      brush_obj_t *brush = (brush_obj_t *)MP_OBJ_TO_PTR(brush_in);
      screen.brush = brush->brush;
      return mp_const_none;
  }

  // mp_obj_t modpicovector_screen() {
  //   return MP_OBJ_FROM_PTR(mp_screen);
  // }

  mp_obj_t modpicovector_draw(mp_obj_t shape_in) {
      shape_obj_t *shape = (shape_obj_t *)MP_OBJ_TO_PTR(shape_in);

      screen.draw(shape->shape);

      return mp_const_none; // It took fifteen years to figure out this was missing.
  }

  mp_obj_t modpicovector_loop(mp_obj_t self_in) {
      self(self_in, modpicovector_obj_t);
      //self->fb->clear();

      return mp_const_none;
  }

  mp_obj_t modpicovector__del__(mp_obj_t self_in) {
      self(self_in, modpicovector_obj_t);
      (void)self;
      return mp_const_none;
  }


}
