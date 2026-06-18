// native/image.cpp — hand-written bodies for the procedural image members.
//
//   load / load_into  decode PNG/JPEG via the bundled decoders
//   window            constructs a sub-view and tracks its parent
//   text / measure_text  branch on the vector vs pixel font
//   shapes            batched shape draw with a reused stack colour brush
//   batch             dispatches (method, *args) command tuples
//
// None of these reduce to "call one C++ function and box the result", so they
// are maintained here. The generated image.cpp provides the per-method drawing
// functions (mpy_image_*) and image_attr, which batch dispatches to.

#include "pv_bindings.hpp"

extern "C" {
  #include "py/stream.h"
  #include "py/reader.h"
  #include "py/runtime.h"
  #include "py/objstr.h"

  // generated drawing functions + attr handler (image.cpp), used by batch
  extern void image_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest);
  extern mp_obj_t mpy_image_clear(size_t, const mp_obj_t *);
  extern mp_obj_t mpy_image_rectangle(size_t, const mp_obj_t *);
  extern mp_obj_t mpy_image_line(size_t, const mp_obj_t *);
  extern mp_obj_t mpy_image_circle(size_t, const mp_obj_t *);
  extern mp_obj_t mpy_image_triangle(size_t, const mp_obj_t *);
  extern mp_obj_t mpy_image_put(size_t, const mp_obj_t *);
  extern mp_obj_t mpy_image_blur(size_t, const mp_obj_t *);
  extern mp_obj_t mpy_image_dither(size_t, const mp_obj_t *);
  extern mp_obj_t mpy_image_shape(size_t, const mp_obj_t *);
  extern mp_obj_t mpy_image_blit(size_t, const mp_obj_t *);
  extern mp_obj_t mpy_image_blit_vspan(size_t, const mp_obj_t *);
  extern mp_obj_t mpy_image_blit_hspan(size_t, const mp_obj_t *);
  extern mp_obj_t image_text(size_t, const mp_obj_t *);

  static void image_open_helper(image_obj_t &target, mp_obj_t path_or_bytes_in,
                                int target_width, int target_height) {
    int status = 0;
    if (mp_obj_is_str(path_or_bytes_in)) {
      const char *path = mp_obj_str_get_str(path_or_bytes_in);
      status = pngdec_open_file(target, path, target_width, target_height);
      if (status == PNG_INVALID_FILE) {
        status = jpegdec_open_file(target, path, target_width, target_height);
      }
    } else {
      mp_buffer_info_t buf;
      mp_get_buffer_raise(path_or_bytes_in, &buf, MP_BUFFER_READ);
      status = pngdec_open_ram(target, buf.buf, buf.len, target_width, target_height);
      if (status == PNG_INVALID_FILE) {
        status = jpegdec_open_ram(target, buf.buf, buf.len, target_width, target_height);
      }
    }
    if (status != 0) {
      mp_raise_msg_varg(&mp_type_ValueError,
                        MP_ERROR_TEXT("unable to read image! %d"), status);
    }
  }

  mp_obj_t image_load(size_t n_args, const mp_obj_t *args) {
    image_obj_t *result = mp_obj_malloc_with_finaliser(image_obj_t, &type_image);
    result->image = nullptr;
    int target_width  = n_args >= 2 ? (int)mp_obj_get_float(args[1]) : 0;
    int target_height = n_args >= 3 ? (int)mp_obj_get_float(args[2]) : 0;
    image_open_helper(*result, args[0], target_width, target_height);
    return MP_OBJ_FROM_PTR(result);
  }

  mp_obj_t image_load_into(size_t n_args, const mp_obj_t *args) {
    self(args[0], image_obj_t);
    image_open_helper(*self, args[1], 0, 0);
    return mp_const_none;
  }

  mp_obj_t image_window(size_t n_args, const mp_obj_t *args) {
    self(args[0], image_obj_t);
    int x, y, w, h;
    if (mp_obj_is_rect(args[1])) {
      rect_t r = mp_obj_get_rect(args[1]);
      x = r.x; y = r.y; w = r.w; h = r.h;
    } else {
      x = mp_obj_get_float(args[1]); y = mp_obj_get_float(args[2]);
      w = mp_obj_get_float(args[3]); h = mp_obj_get_float(args[4]);
    }
    image_obj_t *result = mp_obj_malloc_with_finaliser(image_obj_t, &type_image);
    result->image = new (m_malloc(sizeof(image_t))) image_t(self->image, rect_t(x, y, w, h));
    result->parent = (void *)self;
    return MP_OBJ_FROM_PTR(result);
  }

  mp_obj_t image_text(size_t n_args, const mp_obj_t *args) {
    self(args[0], image_obj_t);
    const char *text = mp_obj_str_get_str(args[1]);
    if (!self->font && !self->pixel_font) {
      mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("target image has no font"));
    }
    size_t _i = 2;
    vec2_t at = pv::get_xy(args, &_i, n_args);
    if (self->font) {
      float size = n_args > _i ? mp_obj_get_float(args[_i]) : 12;
      self->image->font()->draw(self->image, text, at.x, at.y, size);
    }
    if (self->pixel_font) {
      self->image->pixel_font()->draw(self->image, text, at.x, at.y);
    }
    return mp_const_none;
  }

  mp_obj_t image_measure_text(size_t n_args, const mp_obj_t *args) {
    self(args[0], image_obj_t);
    const char *text = mp_obj_str_get_str(args[1]);
    if (!self->font && !self->pixel_font) {
      mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("target image has no font"));
    }
    mp_obj_t result[2];
    if (self->font) {
      float size = mp_obj_get_float(args[2]);
      rect_t r = self->image->font()->measure(self->image, text, size);
      result[0] = mp_obj_new_float(r.w);
      result[1] = mp_obj_new_float(r.h);
    }
    if (self->pixel_font) {
      rect_t r = self->image->pixel_font()->measure(self->image, text);
      result[0] = mp_obj_new_float(r.w);
      result[1] = mp_obj_new_float(r.h);
    }
    return mp_obj_new_tuple(2, result);
  }

  // Batched shape draw: each entry is a shape, or (shape, brush|color). A
  // colour entry is wrapped in a stack-allocated colour brush reused each
  // iteration, so it costs no per-shape heap allocation.
  mp_obj_t image_shapes(size_t n_args, const mp_obj_t *args) {
    self(args[0], image_obj_t);
    size_t len; mp_obj_t *items;
    mp_obj_get_array(args[1], &len, &items);
    brush_t *default_brush = self->image->brush();

    for (size_t i = 0; i < len; i++) {
      mp_obj_t entry = items[i];
      shape_obj_t *shape;
      brush_t *b = default_brush;
      alignas(color_brush_t) char cbuf[sizeof(color_brush_t)];

      if (mp_obj_is_type(entry, &type_shape)) {
        shape = (shape_obj_t *)MP_OBJ_TO_PTR(entry);
      } else {
        size_t tlen; mp_obj_t *t;
        mp_obj_get_array(entry, &tlen, &t);
        if (tlen < 1 || !mp_obj_is_type(t[0], &type_shape)) {
          mp_raise_msg_varg(&mp_type_ValueError,
                            MP_ERROR_TEXT("entry must be a shape or (shape, brush)"));
        }
        shape = (shape_obj_t *)MP_OBJ_TO_PTR(t[0]);
        if (tlen >= 2 && t[1] != mp_const_none) {
          if (mp_obj_is_type(t[1], &type_brush)) {
            b = ((brush_obj_t *)MP_OBJ_TO_PTR(t[1]))->brush;
          } else if (mp_obj_is_type(t[1], &type_color)) {
            color_obj_t *col = (color_obj_t *)MP_OBJ_TO_PTR(t[1]);
            b = new (cbuf) color_brush_t(*col->c);
          } else {
            mp_raise_TypeError(MP_ERROR_TEXT("entry brush must be a brush or color"));
          }
        }
      }
      if (!b) mp_raise_msg_varg(&mp_type_ValueError,
                                MP_ERROR_TEXT("no pen set; supply a brush/color in the entry "
                                              "or set screen.pen first"));
      picovector::render(shape->shape, self->image, &shape->shape->transform, b);
    }
    return mp_const_none;
  }

  // Run a list of (command, *args) tuples in one C loop. A single-argument
  // command first tries to set an attribute (pen, clip, …); otherwise the
  // command name selects a generated drawing function.
  mp_obj_t image_batch(size_t n_args, const mp_obj_t *args) {
    if (!mp_obj_is_type(args[1], &mp_type_list)) {
      mp_raise_TypeError(MP_ERROR_TEXT("invalid parameters, expected list of draw commands"));
    }
    mp_obj_t handler_args[32];
    handler_args[0] = args[0];

    size_t ncommands; mp_obj_t *commands;
    mp_obj_list_get(args[1], &ncommands, &commands);

    for (size_t i = 0; i < ncommands; i++) {
      if (!mp_obj_is_type(commands[i], &mp_type_tuple)) {
        mp_raise_TypeError(MP_ERROR_TEXT("list entries must be tuples in the format "
                                         "(command, parameter1, parameter2, ...)"));
      }
      size_t ncommand; mp_obj_t *command;
      mp_obj_tuple_get(commands[i], &ncommand, &command);
      qstr name = mp_obj_str_get_qstr(command[0]);

      size_t nparameters = ncommand - 1;
      for (size_t j = 1; j < ncommand; j++) handler_args[j] = command[j];

      if (nparameters == 1) {
        mp_obj_t dest[2];
        dest[0] = MP_OBJ_SENTINEL;
        dest[1] = handler_args[1];
        image_attr(handler_args[0], name, dest);
        if (dest[0] == MP_OBJ_NULL) continue;  // attribute set, next command
      }

      size_t nh = nparameters + 1;
      switch (name) {
        case MP_QSTR_clear:      mpy_image_clear(nh, handler_args); break;
        case MP_QSTR_rectangle:  mpy_image_rectangle(nh, handler_args); break;
        case MP_QSTR_line:       mpy_image_line(nh, handler_args); break;
        case MP_QSTR_circle:     mpy_image_circle(nh, handler_args); break;
        case MP_QSTR_triangle:   mpy_image_triangle(nh, handler_args); break;
        case MP_QSTR_put:        mpy_image_put(nh, handler_args); break;
        case MP_QSTR_blur:       mpy_image_blur(nh, handler_args); break;
        case MP_QSTR_dither:     mpy_image_dither(nh, handler_args); break;
        case MP_QSTR_shape:      mpy_image_shape(nh, handler_args); break;
        case MP_QSTR_text:       image_text(nh, handler_args); break;
        case MP_QSTR_blit_vspan: mpy_image_blit_vspan(nh, handler_args); break;
        case MP_QSTR_blit_hspan: mpy_image_blit_hspan(nh, handler_args); break;
        case MP_QSTR_blit:       mpy_image_blit(nh, handler_args); break;
        default: mp_raise_ValueError(MP_ERROR_TEXT("unknown method"));
      }
    }
    return mp_const_none;
  }

}
