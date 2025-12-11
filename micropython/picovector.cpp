#include "picovector.hpp"
#include "mp_helpers.hpp"

action_t m_attr_action(mp_obj_t *dest) {
  if(dest[0] == MP_OBJ_NULL && dest[1] == MP_OBJ_NULL) {return GET;}
  if(dest[0] == MP_OBJ_NULL && dest[1] != MP_OBJ_NULL) {return DELETE;}
  return SET;
}

uint32_t ru32(mp_obj_t file) {
  int error;
  uint32_t result;
  mp_stream_read_exactly(file, &result, 4, &error);
  return __builtin_bswap32(result);
}

uint16_t ru16(mp_obj_t file) {
  int error;
  uint16_t result;
  mp_stream_read_exactly(file, &result, 2, &error);
  return __builtin_bswap16(result);
}

uint8_t ru8(mp_obj_t file) {
  int error;
  uint8_t result;
  mp_stream_read_exactly(file, &result, 1, &error);
  return result;
}

int8_t rs8(mp_obj_t file) {
  int error;
  int8_t result;
  mp_stream_read_exactly(file, &result, 1, &error);
  return result;
}

extern "C" {
  #include "py/runtime.h"
  image_obj_t *default_target;

  mp_obj_t modpicovector___init__(void) {
      return mp_const_none;
  }

  brush_obj_t *mp_obj_to_brush(size_t n_args, const mp_obj_t *args) {
    if(n_args == 1 && mp_obj_is_type(args[0], &type_brush)) {
      return (brush_obj_t *)args[0];
    }

    if(n_args == 1 && mp_obj_is_type(args[0], &type_color)) {
      color_obj_t *color = (color_obj_t *)MP_OBJ_TO_PTR(args[0]);
      brush_obj_t *brush = mp_obj_malloc(brush_obj_t, &type_brush);
      brush->brush = m_new_class(color_brush, color->c);
      return brush;
    }
    if(n_args >= 3 && mp_obj_is_int(args[0]) && mp_obj_is_int(args[1]) && mp_obj_is_int(args[2])) {
      brush_obj_t *brush = mp_obj_malloc(brush_obj_t, &type_brush);
      int r = mp_obj_get_int(args[0]);
      int g = mp_obj_get_int(args[1]);
      int b = mp_obj_get_int(args[2]);
      int a = (n_args > 3 && mp_obj_is_int(args[3])) ? mp_obj_get_int(args[3]) : 255;
      brush->brush = m_new_class(color_brush, r, g, b, a);
      return brush;
    }

    return nullptr;
  }

  mp_obj_t modpicovector_pen(size_t n_args, const mp_obj_t *args) {
    brush_obj_t *new_brush = mp_obj_to_brush(n_args, args);

    if(!new_brush){
      mp_raise_TypeError(MP_ERROR_TEXT("value must be of type brush or color"));
    }

    // TODO: This should set a GLOBAL brush along with other state on
    // picovector, and the default target (an image) should then use the
    // global brush for painting
    if(default_target) {
      default_target->brush = new_brush;
      default_target->image->brush(default_target->brush->brush);
    }
    return mp_const_none;
  }

  mp_obj_t modpicovector_dda(size_t n_args, const mp_obj_t *pos_args) {
    float x = mp_obj_get_float(pos_args[0]);
    float y = mp_obj_get_float(pos_args[1]);
    float dx = mp_obj_get_float(pos_args[2]);
    float dy = mp_obj_get_float(pos_args[3]);

    size_t map_height;
    mp_obj_t *map_rows;
    mp_obj_get_array(pos_args[4], &map_height, &map_rows);
    size_t map_width = mp_obj_get_int(mp_obj_len(map_rows[0]));

    int max = mp_obj_get_int(pos_args[5]);

    int ix = floor(x);
    int iy = floor(y);

    const float eps = 1e-30f;
    float inv_dx = fabs(dx) > eps ? 1.0f / dx : 1e30f;
    float inv_dy = fabs(dy) > eps ? 1.0f / dy : 1e30f;

    int step_x = (dx > 0.0f) ? 1 : (dx < 0.0f ? -1 : 0);
    int step_y = (dy > 0.0f) ? 1 : (dy < 0.0f ? -1 : 0);

    float t_delta_x = fabs(inv_dx);
    float t_delta_y = fabs(inv_dy);

    float t_max_x = 1e30f;
    float t_max_y = 1e30f;

    if(step_x > 0) {
      t_max_x = ((float(ix) + 1.0f) - x) * inv_dx;
    } else if(step_x < 0) {
      t_max_x = ((float(ix) - x)) * inv_dx;
    }

    if(step_y > 0) {
      t_max_y = ((float(iy) + 1.0f) - y) * inv_dy;
    } else if(step_y < 0) {
      t_max_y = ((float(iy) - y)) * inv_dy;
    }

    float t_enter = 0.0f;

    mp_obj_t result = mp_obj_new_list(0, NULL);

    //mp_obj_t result = mp_obj_new_list(0, NULL);

    int i = 0;
    while (t_enter <= max) {
      float t_exit = std::min(t_max_x, t_max_y);

      // calculate the intersection position
      float hit_x = x + dx * t_exit;
      float hit_y = y + dy * t_exit;

      // calculate the edge which the intersection occured on (0=top, 1=right, 2=bottom, 3=left)
      bool vertical = abs(hit_y - round(hit_y)) > abs(hit_x - round(hit_x));
      int edge = vertical ? (dx < 0 ? 3 : 1) : (dy < 0 ? 0 : 2);

      // calculate the intersection offset
      float offset = vertical ? (hit_y - floor(hit_y)) : (hit_x - floor(hit_x));

      float distance = sqrt(pow(hit_x - x, 2) + pow(hit_y - y, 2));

      // calculate grid square of intersection
      int gx, gy;
      if(vertical) {
        gx = int(hit_x + (edge == 3 ? -0.5f : 0.5f));
        gy = int(hit_y);
      } else {
        gx = int(hit_x);
        gy = int(hit_y + (edge == 0 ? -0.5f : 0.5f));
      }

      if(gy < 0 || gy >= int(map_height)) {
        break;
      }

      size_t row_width;
      mp_obj_t *map_row;
      mp_obj_get_array(map_rows[gy], &row_width, &map_row);

      if(gx < 0 || gx >= int(row_width)) {
        break;
      }

      int tile_id = mp_obj_get_int(map_row[gx]);

      if(tile_id > 0) {
        // if solid then create intersection entry
        mp_obj_tuple_t *t = (mp_obj_tuple_t*)MP_OBJ_TO_PTR(mp_obj_new_tuple(8, NULL));
        t->items[0] = mp_obj_new_float(hit_x);
        t->items[1] = mp_obj_new_float(hit_y);
        t->items[2] = mp_obj_new_int(gx);
        t->items[3] = mp_obj_new_int(gy);
        t->items[4] = mp_obj_new_int(edge);
        t->items[5] = mp_obj_new_float(offset);
        t->items[6] = mp_obj_new_float(distance);
        t->items[7] = mp_obj_new_int(tile_id);

        mp_obj_list_append(result, MP_OBJ_FROM_PTR(t));

        i++;
        if(i >= max) {
          break;
        }
      }

      // Step to the next cell: whichever boundary we hit first
      if (t_max_x < t_max_y) {
        ix += step_x;
        t_enter = t_max_x;
        t_max_x += t_delta_x; // next vertical boundary
      } else {
        iy += step_y;
        t_enter = t_max_y;
        t_max_y += t_delta_y; // next horizontal boundary
      }
    }

    return MP_OBJ_FROM_PTR(result);
  }


  void modpicovector_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    size_t action = m_attr_action(dest);

    switch(attr) {
      case MP_QSTR_default_target: {
        if(action == SET) {
          if(!mp_obj_is_type(dest[1], &type_image)) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("invalid parameter, expected image"));
          }
          default_target = (image_obj_t *)dest[1];
          dest[0] = MP_OBJ_NULL;
          return;
        } else {
          dest[0] = MP_OBJ_FROM_PTR(default_target);
        }
      };
    }
  }


}
