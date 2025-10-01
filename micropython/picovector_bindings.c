
#include "py/runtime.h"


// extern mp_obj_t modpicovector_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args);
extern mp_obj_t modpicovector_loop(mp_obj_t self_in);
extern mp_obj_t modpicovector__del__(mp_obj_t self_in);

static MP_DEFINE_CONST_FUN_OBJ_1(modpicovector_loop_obj, modpicovector_loop);
static MP_DEFINE_CONST_FUN_OBJ_1(modpicovector__del___obj, modpicovector__del__);


extern mp_obj_t modpicovector_init(size_t n_args, const mp_obj_t *pos_args);
static MP_DEFINE_CONST_FUN_OBJ_VAR(modpicovector_init_obj, 0, modpicovector_init);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(modpicovector_init_static_obj, MP_ROM_PTR(&modpicovector_init_obj));


extern mp_obj_t modpicovector_draw(mp_obj_t shape_in);
static MP_DEFINE_CONST_FUN_OBJ_1(modpicovector_draw_obj, modpicovector_draw);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(modpicovector_draw_static_obj, MP_ROM_PTR(&modpicovector_draw_obj));

// extern mp_obj_t modpicovector_screen();
// static MP_DEFINE_CONST_FUN_OBJ_0(modpicovector_screen_obj, modpicovector_screen);
// static MP_DEFINE_CONST_STATICMETHOD_OBJ(modpicovector_screen_static_obj, MP_ROM_PTR(&modpicovector_screen_obj));



static const mp_rom_map_elem_t modpicovector_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_loop), MP_ROM_PTR(&modpicovector_loop_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&modpicovector__del___obj) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&modpicovector_init_static_obj) },
};
static MP_DEFINE_CONST_DICT(modpicovector_locals_dict, modpicovector_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    type_PicoVector,
    MP_QSTR_PicoVector,
    MP_TYPE_FLAG_NONE,
    //make_new, modpicovector_make_new,
    locals_dict, &modpicovector_locals_dict
);

extern const mp_obj_type_t type_Shapes;
extern const mp_obj_type_t type_Brushes;
extern const mp_obj_type_t type_Image;

static const mp_rom_map_elem_t modpicovector_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_modpicovector) },
    { MP_ROM_QSTR(MP_QSTR_PicoVector),  MP_ROM_PTR(&type_PicoVector) },
    { MP_ROM_QSTR(MP_QSTR_brushes),  MP_ROM_PTR(&type_Brushes) },
    { MP_ROM_QSTR(MP_QSTR_shapes),  MP_ROM_PTR(&type_Shapes) },
    { MP_ROM_QSTR(MP_QSTR_Image),  MP_ROM_PTR(&type_Image) },
    { MP_ROM_QSTR(MP_QSTR_draw),  MP_ROM_PTR(&modpicovector_draw_static_obj) },
};
static MP_DEFINE_CONST_DICT(modpicovector_globals, modpicovector_globals_table);

// Define module object.
const mp_obj_module_t modpicovector = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&modpicovector_globals,
};

MP_REGISTER_MODULE(MP_QSTR_picovector, modpicovector);