
#include "../picovector.hpp"

#ifdef PICO
#include "pico/stdlib.h"
#endif

#define self(self_in, T) T *self = (T *)MP_OBJ_TO_PTR(self_in)

using namespace picovector;

extern "C" {
  #include "py/stream.h"
  #include "py/reader.h"
  #include "py/runtime.h"
  #include "extmod/vfs.h"
  #include "py/mphal.h"

  extern const mp_obj_type_t type_Input;

  enum BUTTON : uint8_t {
    HOME = 0b100000,
    A    = 0b010000,
    B    = 0b001000,
    C    = 0b000100,
    UP   = 0b000010,
    DOWN = 0b000001
  };

#ifdef PICO
  uint8_t get_buttons() {
    uint8_t buttons = gpio_get(BW_SWITCH_A) ? 0 : BUTTON::A;
      buttons |= gpio_get(BW_SWITCH_B) ? 0 : BUTTON::B;
      buttons |= gpio_get(BW_SWITCH_C) ? 0 : BUTTON::C;
      buttons |= gpio_get(BW_SWITCH_UP) ? 0 : BUTTON::UP;
      buttons |= gpio_get(BW_SWITCH_DOWN) ? 0 : BUTTON::DOWN;
      buttons |= gpio_get(BW_SWITCH_HOME) ? 0 : BUTTON::HOME;
    return buttons;
  }
  uint8_t picovector_buttons;
  uint8_t picovector_changed_buttons;
  mp_uint_t picovector_ticks;
  mp_uint_t picovector_last_ticks;
#else
  extern uint8_t picovector_buttons;
  extern uint8_t picovector_changed_buttons;
  extern double picovector_ticks;
  extern double picovector_last_ticks;
#endif

  typedef struct _Input_obj_t {
    mp_obj_base_t base;
  } input_obj_t;

  static mp_obj_t input_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    input_obj_t *self = m_new_obj(input_obj_t);
    self->base.type = type;
    return MP_OBJ_FROM_PTR(self);
  }

  static void input_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    self(self_in, input_obj_t);

    if(attr == MP_QSTR_ticks && dest[0] == MP_OBJ_NULL) {
      dest[0] = mp_obj_new_int_from_ll(picovector_ticks);
      return;
    }

    if(attr == MP_QSTR_ticks_delta && dest[0] == MP_OBJ_NULL) {
      dest[0] = mp_obj_new_int_from_ll(picovector_ticks - picovector_last_ticks);
      return;
    }

    if((attr == MP_QSTR_held || attr == MP_QSTR_pressed || attr == MP_QSTR_released || attr == MP_QSTR_changed) && dest[0] == MP_OBJ_NULL) {
      mp_obj_t t_items[6];
      uint8_t buttons = 0;

      switch(attr) {
        case MP_QSTR_held:
          buttons = picovector_buttons;
          break;
        case MP_QSTR_pressed:
          buttons = picovector_buttons & picovector_changed_buttons;
          break;
        case MP_QSTR_released:
          buttons = ~picovector_buttons & picovector_changed_buttons;
          break;
        case MP_QSTR_changed:
          buttons = picovector_changed_buttons;
          break;
        default:
          break;
      }
      int n = 0;
      if(buttons & BUTTON::HOME) {
        t_items[n] = mp_obj_new_int(BUTTON::HOME);
        n++;
      }
      if(buttons & BUTTON::A) {
        t_items[n] = mp_obj_new_int(BUTTON::A);
        n++;
      }
      if(buttons & BUTTON::B) {
        t_items[n] = mp_obj_new_int(BUTTON::B);
        n++;
      }
      if(buttons & BUTTON::C) {
        t_items[n] = mp_obj_new_int(BUTTON::C);
        n++;
      }
      if(buttons & BUTTON::UP) {
        t_items[n] = mp_obj_new_int(BUTTON::UP);
        n++;
      }
      if(buttons & BUTTON::DOWN) {
        t_items[n] = mp_obj_new_int(BUTTON::DOWN);
        n++;
      }
      dest[0] = mp_obj_new_tuple(n, t_items);
      return;
    }

    dest[1] = MP_OBJ_SENTINEL;
  }

#ifdef PICO
  // Call io.poll() to set up frame stable input and tick values
  mp_obj_t input_poll(mp_obj_t self_in) {
    self(self_in, input_obj_t);
    uint8_t buttons = get_buttons();
    picovector_changed_buttons = buttons ^ picovector_buttons;
    picovector_buttons = buttons;
    picovector_last_ticks = picovector_ticks;
    picovector_ticks = mp_hal_ticks_ms();
    return mp_const_none;
  }
  static MP_DEFINE_CONST_FUN_OBJ_1(input_poll_obj, input_poll);
#endif

  static const mp_rom_map_elem_t input_locals_dict_table[] = {
      // TODO ... v How does it know!?!? v
      //{ MP_ROM_QSTR(MP_QSTR_pressed),     MP_ROM_PTR(&input_pressed_obj) },
      //{ MP_ROM_QSTR(MP_QSTR_ticks),       MP_OBJ_SENTINEL },
      { MP_ROM_QSTR(MP_QSTR_BUTTON_HOME), MP_ROM_INT(BUTTON::HOME) },
      { MP_ROM_QSTR(MP_QSTR_BUTTON_A),    MP_ROM_INT(BUTTON::A) },
      { MP_ROM_QSTR(MP_QSTR_BUTTON_B),    MP_ROM_INT(BUTTON::B) },
      { MP_ROM_QSTR(MP_QSTR_BUTTON_C),    MP_ROM_INT(BUTTON::C) },
      { MP_ROM_QSTR(MP_QSTR_BUTTON_UP),   MP_ROM_INT(BUTTON::UP) },
      { MP_ROM_QSTR(MP_QSTR_BUTTON_DOWN), MP_ROM_INT(BUTTON::DOWN) },
#ifdef PICO
      { MP_ROM_QSTR(MP_QSTR_poll), MP_ROM_PTR(&input_poll_obj) },
#endif
  };
  static MP_DEFINE_CONST_DICT(input_locals_dict, input_locals_dict_table);

  MP_DEFINE_CONST_OBJ_TYPE(
      type_Input,
      MP_QSTR_Input,
      MP_TYPE_FLAG_NONE,
      make_new, (const void *)input_new,
      attr, (const void *)input_attr,
      locals_dict, &input_locals_dict
  );

}
