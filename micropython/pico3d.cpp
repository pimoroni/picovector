// MicroPython bindings for the pico3d engine (see ../pico3d.hpp).
//
// Exposes a `pico3d` module with: mat4, vec3, mesh, material, light, target,
// the helpers perspective()/look_at()/rgb(), and the FLAT/GOURAUD/UNLIT and
// NEAREST/BILINEAR enum constants.
//
// Colours are plain packed ints in the framebuffer's 0x00BBGGRR order — build
// them with pico3d.rgb(r, g, b). Geometry buffers are borrowed (not copied):
// the mesh roots the Python buffer objects so the GC can't free them while the
// mesh is alive. The render target wraps a picovector image and owns a uint16
// depth buffer sized to it.

#include "mp_tracked_allocator.hpp"

#include "mp_helpers.hpp"
#include "picovector.hpp"   // image_t, image_obj_t, type_image, mat3 etc.
#include "../pico3d.hpp"    // engine API + vec3_t / vec4_t / mat4_t

using namespace picovector;

extern "C" {

  #include "py/runtime.h"

  // ---- object types --------------------------------------------------------

  typedef struct _pico3d_mat4_obj_t { mp_obj_base_t base; mat4_t m; } mat4_obj_t;
  typedef struct _pico3d_vec3_obj_t { mp_obj_base_t base; vec3_t v; } vec3_obj_t;

  typedef struct _pico3d_mesh_obj_t {
    mp_obj_base_t base;
    pico3d_mesh_t mesh;
    mp_obj_t positions_ref, normals_ref, uvs_ref, colors_ref, tangents_ref, indices_ref; // GC roots
  } mesh_obj_t;

  typedef struct _pico3d_material_obj_t {
    mp_obj_base_t base;
    pico3d_material_t mat;
    pico3d_texture_t  tex;
    pico3d_texture_t  nmap;                                    // normal-map view
    pico3d_texture_t  mcap;                                    // matcap view
    pico3d_shading_t  shading;
    mp_obj_t texture_ref, normal_ref, matcap_ref;             // GC roots
  } material_obj_t;

  typedef struct _pico3d_light_obj_t { mp_obj_base_t base; pico3d_light_t light; } light_obj_t;

  typedef struct _pico3d_target_obj_t {
    mp_obj_base_t base;
    mp_obj_t  image_ref;                                       // GC root
    uint16_t *depth;
    int w, h;
    pico3d_vcache_t *vcache;                                   // vertex transform scratch
    uint32_t vcache_cap;                                       // capacity in vertices
  } target_obj_t;

  extern const mp_obj_type_t type_pico3d_mat4;
  extern const mp_obj_type_t type_pico3d_vec3;
  extern const mp_obj_type_t type_pico3d_mesh;
  extern const mp_obj_type_t type_pico3d_material;
  extern const mp_obj_type_t type_pico3d_light;
  extern const mp_obj_type_t type_pico3d_target;

  // ---- vec3 ----------------------------------------------------------------

  static mp_obj_t make_vec3(vec3_t v) {
    vec3_obj_t *r = mp_obj_malloc(vec3_obj_t, &type_pico3d_vec3);
    r->v = v;
    return MP_OBJ_FROM_PTR(r);
  }

  static vec3_t get_vec3(mp_obj_t o) {
    if(!mp_obj_is_type(o, &type_pico3d_vec3))
      mp_raise_msg(&mp_type_TypeError, MP_ERROR_TEXT("expected a vec3"));
    return ((vec3_obj_t *)MP_OBJ_TO_PTR(o))->v;
  }

  MPY_BIND_NEW(pico3d_vec3, {
    vec3_obj_t *self = mp_obj_malloc(vec3_obj_t, type);
    self->v = vec3_t();
    if(n_args == 3) {
      self->v.x = mp_obj_get_float(args[0]);
      self->v.y = mp_obj_get_float(args[1]);
      self->v.z = mp_obj_get_float(args[2]);
    } else if(n_args != 0) {
      mp_raise_msg(&mp_type_TypeError, MP_ERROR_TEXT("expected vec3() or vec3(x, y, z)"));
    }
    return MP_OBJ_FROM_PTR(self);
  })

  MPY_BIND_VAR(1, normalized, {
    vec3_obj_t *self = (vec3_obj_t *)MP_OBJ_TO_PTR(args[0]);
    return make_vec3(self->v.normalized());
  })
  MPY_BIND_VAR(1, length, {
    vec3_obj_t *self = (vec3_obj_t *)MP_OBJ_TO_PTR(args[0]);
    return mp_obj_new_float(self->v.length());
  })
  MPY_BIND_VAR(2, dot, {
    vec3_obj_t *self = (vec3_obj_t *)MP_OBJ_TO_PTR(args[0]);
    return mp_obj_new_float(self->v.dot(get_vec3(args[1])));
  })
  MPY_BIND_VAR(2, cross, {
    vec3_obj_t *self = (vec3_obj_t *)MP_OBJ_TO_PTR(args[0]);
    return make_vec3(self->v.cross(get_vec3(args[1])));
  })
  MPY_BIND_VAR(3, lerp, {
    vec3_obj_t *self = (vec3_obj_t *)MP_OBJ_TO_PTR(args[0]);
    return make_vec3(self->v.lerp(get_vec3(args[1]), mp_obj_get_float(args[2])));
  })

  MPY_BIND_ATTR(vec3, {
    self(self_in, vec3_obj_t);
    action_t action = m_attr_action(dest);
    switch(attr | action) {
      case MP_QSTR_x | GET: dest[0] = mp_obj_new_float(self->v.x); return;
      case MP_QSTR_x | SET: self->v.x = mp_obj_get_float(dest[1]); dest[0] = MP_OBJ_NULL; return;
      case MP_QSTR_y | GET: dest[0] = mp_obj_new_float(self->v.y); return;
      case MP_QSTR_y | SET: self->v.y = mp_obj_get_float(dest[1]); dest[0] = MP_OBJ_NULL; return;
      case MP_QSTR_z | GET: dest[0] = mp_obj_new_float(self->v.z); return;
      case MP_QSTR_z | SET: self->v.z = mp_obj_get_float(dest[1]); dest[0] = MP_OBJ_NULL; return;
    }
    dest[1] = MP_OBJ_SENTINEL;
  })

  static mp_obj_t vec3_binary_op(mp_binary_op_t op, mp_obj_t lhs_in, mp_obj_t rhs_in) {
    vec3_obj_t *lhs = (vec3_obj_t *)MP_OBJ_TO_PTR(lhs_in);
    switch(op) {
      case MP_BINARY_OP_ADD:
        if(mp_obj_is_type(rhs_in, &type_pico3d_vec3))
          return make_vec3(lhs->v + ((vec3_obj_t *)MP_OBJ_TO_PTR(rhs_in))->v);
        break;
      case MP_BINARY_OP_SUBTRACT:
        if(mp_obj_is_type(rhs_in, &type_pico3d_vec3))
          return make_vec3(lhs->v - ((vec3_obj_t *)MP_OBJ_TO_PTR(rhs_in))->v);
        break;
      case MP_BINARY_OP_MULTIPLY:
        if(mp_obj_is_int(rhs_in) || mp_obj_is_float(rhs_in))
          return make_vec3(lhs->v * mp_obj_get_float(rhs_in));
        break;
      default: return MP_OBJ_NULL;
    }
    return MP_OBJ_NULL;
  }

  static void vec3_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    self(self_in, vec3_obj_t);
    mp_printf(print, "vec3(%f, %f, %f)", self->v.x, self->v.y, self->v.z);
  }

  MPY_BIND_LOCALS_DICT(vec3,
    MPY_BIND_ROM_PTR(normalized),
    MPY_BIND_ROM_PTR(length),
    MPY_BIND_ROM_PTR(dot),
    MPY_BIND_ROM_PTR(cross),
    MPY_BIND_ROM_PTR(lerp),
  )

  MP_DEFINE_CONST_OBJ_TYPE(
      type_pico3d_vec3, MP_QSTR_vec3, MP_TYPE_FLAG_NONE,
      make_new, (const void *)pico3d_vec3_new,
      print, (const void *)vec3_print,
      binary_op, (const void *)vec3_binary_op,
      attr, (const void *)vec3_attr,
      locals_dict, &vec3_locals_dict);

  // ---- mat4 ----------------------------------------------------------------

  static mp_obj_t make_mat4(const mat4_t &m) {
    mat4_obj_t *r = mp_obj_malloc(mat4_obj_t, &type_pico3d_mat4);
    r->m = m;
    return MP_OBJ_FROM_PTR(r);
  }

  MPY_BIND_NEW(pico3d_mat4, {
    (void)n_args; (void)args;
    mat4_obj_t *self = mp_obj_malloc(mat4_obj_t, type);
    self->m = mat4_t();                 // identity
    return MP_OBJ_FROM_PTR(self);
  })

  // Each builder returns a NEW matrix (the receiver is left unchanged), so
  // mat4().translate(...).rotate_y(...) chains cleanly.
  MPY_BIND_VAR(4, translate, {
    mat4_obj_t *self = (mat4_obj_t *)MP_OBJ_TO_PTR(args[0]);
    mat4_t m = self->m;
    m.translate(mp_obj_get_float(args[1]), mp_obj_get_float(args[2]), mp_obj_get_float(args[3]));
    return make_mat4(m);
  })
  MPY_BIND_VAR(2, scale, {
    mat4_obj_t *self = (mat4_obj_t *)MP_OBJ_TO_PTR(args[0]);
    mat4_t m = self->m;
    if(n_args >= 4) m.scale(mp_obj_get_float(args[1]), mp_obj_get_float(args[2]), mp_obj_get_float(args[3]));
    else            m.scale(mp_obj_get_float(args[1]));
    return make_mat4(m);
  })
  MPY_BIND_VAR(2, rotate_x, {
    mat4_obj_t *self = (mat4_obj_t *)MP_OBJ_TO_PTR(args[0]);
    mat4_t m = self->m; m.rotate_x(mp_obj_get_float(args[1])); return make_mat4(m);
  })
  MPY_BIND_VAR(2, rotate_y, {
    mat4_obj_t *self = (mat4_obj_t *)MP_OBJ_TO_PTR(args[0]);
    mat4_t m = self->m; m.rotate_y(mp_obj_get_float(args[1])); return make_mat4(m);
  })
  MPY_BIND_VAR(2, rotate_z, {
    mat4_obj_t *self = (mat4_obj_t *)MP_OBJ_TO_PTR(args[0]);
    mat4_t m = self->m; m.rotate_z(mp_obj_get_float(args[1])); return make_mat4(m);
  })
  MPY_BIND_VAR(2, multiply, {
    mat4_obj_t *self  = (mat4_obj_t *)MP_OBJ_TO_PTR(args[0]);
    if(!mp_obj_is_type(args[1], &type_pico3d_mat4))
      mp_raise_msg(&mp_type_TypeError, MP_ERROR_TEXT("expected a mat4"));
    mat4_obj_t *other = (mat4_obj_t *)MP_OBJ_TO_PTR(args[1]);
    mat4_t m = self->m; m.multiply(other->m); return make_mat4(m);
  })

  static mp_obj_t mat4_binary_op(mp_binary_op_t op, mp_obj_t lhs_in, mp_obj_t rhs_in) {
    if(op == MP_BINARY_OP_MULTIPLY && mp_obj_is_type(rhs_in, &type_pico3d_mat4)) {
      mat4_obj_t *lhs = (mat4_obj_t *)MP_OBJ_TO_PTR(lhs_in);
      mat4_obj_t *rhs = (mat4_obj_t *)MP_OBJ_TO_PTR(rhs_in);
      mat4_t m = lhs->m; m.multiply(rhs->m); return make_mat4(m);
    }
    return MP_OBJ_NULL;
  }

  MPY_BIND_LOCALS_DICT(mat4,
    MPY_BIND_ROM_PTR(translate),
    MPY_BIND_ROM_PTR(scale),
    MPY_BIND_ROM_PTR(rotate_x),
    MPY_BIND_ROM_PTR(rotate_y),
    MPY_BIND_ROM_PTR(rotate_z),
    MPY_BIND_ROM_PTR(multiply),
  )

  MP_DEFINE_CONST_OBJ_TYPE(
      type_pico3d_mat4, MP_QSTR_mat4, MP_TYPE_FLAG_NONE,
      make_new, (const void *)pico3d_mat4_new,
      binary_op, (const void *)mat4_binary_op,
      locals_dict, &mat4_locals_dict);

  // ---- mesh ----------------------------------------------------------------

  static const float *buffer_floats(mp_obj_t o, uint32_t *out_floats) {
    mp_buffer_info_t bi;
    mp_get_buffer_raise(o, &bi, MP_BUFFER_READ);
    *out_floats = bi.len / sizeof(float);
    return (const float *)bi.buf;
  }

  static mp_obj_t pico3d_mesh_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_positions, ARG_indices, ARG_normals, ARG_uvs, ARG_colors, ARG_tangents };
    static const mp_arg_t allowed[] = {
      { MP_QSTR_positions, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
      { MP_QSTR_indices,   MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
      { MP_QSTR_normals,   MP_ARG_OBJ,                   {.u_obj = mp_const_none} },
      { MP_QSTR_uvs,       MP_ARG_OBJ,                   {.u_obj = mp_const_none} },
      { MP_QSTR_colors,    MP_ARG_OBJ,                   {.u_obj = mp_const_none} },
      { MP_QSTR_tangents,  MP_ARG_OBJ,                   {.u_obj = mp_const_none} },
    };
    mp_arg_val_t vals[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed), allowed, vals);

    mesh_obj_t *self = mp_obj_malloc(mesh_obj_t, type);
    self->normals_ref = mp_const_none; self->uvs_ref = mp_const_none;
    self->colors_ref = mp_const_none; self->tangents_ref = mp_const_none;
    pico3d_mesh_t &m = self->mesh;
    m.normals = nullptr; m.uvs = nullptr; m.colors = nullptr; m.tangents = nullptr;

    uint32_t nfloats;
    m.positions = buffer_floats(vals[ARG_positions].u_obj, &nfloats);
    m.vertex_count = nfloats / 3;
    self->positions_ref = vals[ARG_positions].u_obj;

    mp_buffer_info_t bi;
    mp_get_buffer_raise(vals[ARG_indices].u_obj, &bi, MP_BUFFER_READ);
    m.indices = (const uint16_t *)bi.buf;
    m.triangle_count = (bi.len / sizeof(uint16_t)) / 3;
    self->indices_ref = vals[ARG_indices].u_obj;

    if(vals[ARG_normals].u_obj != mp_const_none) {
      uint32_t n; m.normals = buffer_floats(vals[ARG_normals].u_obj, &n);
      self->normals_ref = vals[ARG_normals].u_obj;
    }
    if(vals[ARG_uvs].u_obj != mp_const_none) {
      uint32_t n; m.uvs = buffer_floats(vals[ARG_uvs].u_obj, &n);
      self->uvs_ref = vals[ARG_uvs].u_obj;
    }
    if(vals[ARG_colors].u_obj != mp_const_none) {
      mp_buffer_info_t cbi;
      mp_get_buffer_raise(vals[ARG_colors].u_obj, &cbi, MP_BUFFER_READ);
      m.colors = (const uint32_t *)cbi.buf;   // one 0x00BBGGRR word per vertex
      self->colors_ref = vals[ARG_colors].u_obj;
    }
    if(vals[ARG_tangents].u_obj != mp_const_none) {
      uint32_t n; m.tangents = buffer_floats(vals[ARG_tangents].u_obj, &n);  // 3/vertex
      self->tangents_ref = vals[ARG_tangents].u_obj;
    }
    return MP_OBJ_FROM_PTR(self);
  }

  MPY_BIND_ATTR(mesh, {
    self(self_in, mesh_obj_t);
    if(m_attr_action(dest) != GET) { dest[1] = MP_OBJ_SENTINEL; return; }
    switch(attr) {
      case MP_QSTR_vertex_count:   dest[0] = mp_obj_new_int(self->mesh.vertex_count);   return;
      case MP_QSTR_triangle_count: dest[0] = mp_obj_new_int(self->mesh.triangle_count); return;
    }
    dest[1] = MP_OBJ_SENTINEL;
  })

  MP_DEFINE_CONST_OBJ_TYPE(
      type_pico3d_mesh, MP_QSTR_mesh, MP_TYPE_FLAG_NONE,
      make_new, (const void *)pico3d_mesh_make_new,
      attr, (const void *)mesh_attr);

  // ---- material ------------------------------------------------------------

  static mp_obj_t pico3d_material_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_color, ARG_texture, ARG_shading, ARG_filter, ARG_double_sided, ARG_alpha_cutoff, ARG_normal_map, ARG_matcap, ARG_specular, ARG_shininess };
    static const mp_arg_t allowed[] = {
      { MP_QSTR_color,        MP_ARG_INT, {.u_int = 0xffffff} },
      { MP_QSTR_texture,      MP_ARG_OBJ, {.u_obj = mp_const_none} },
      { MP_QSTR_shading,      MP_ARG_INT, {.u_int = PICO3D_FLAT} },
      { MP_QSTR_filter,       MP_ARG_INT, {.u_int = PICO3D_NEAREST} },
      { MP_QSTR_double_sided, MP_ARG_BOOL,{.u_bool = false} },
      { MP_QSTR_alpha_cutoff, MP_ARG_INT, {.u_int = 128} },  // 0 disables alpha-cutout
      { MP_QSTR_normal_map,   MP_ARG_OBJ, {.u_obj = mp_const_none} },
      { MP_QSTR_matcap,       MP_ARG_OBJ, {.u_obj = mp_const_none} },
      { MP_QSTR_specular,     MP_ARG_INT, {.u_int = 0} },     // 0x00BBGGRR; 0 = no specular
      { MP_QSTR_shininess,    MP_ARG_INT, {.u_int = 32} },    // Blinn-Phong exponent
    };
    mp_arg_val_t vals[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed), allowed, vals);

    material_obj_t *self = mp_obj_malloc(material_obj_t, type);
    self->texture_ref = mp_const_none;
    self->normal_ref = mp_const_none;
    self->matcap_ref = mp_const_none;
    self->shading = (pico3d_shading_t)vals[ARG_shading].u_int;
    self->mat.texture = nullptr;
    self->mat.normal_map = nullptr;
    self->mat.matcap = nullptr;
    self->mat.color = (uint32_t)vals[ARG_color].u_int;
    self->mat.filter = (pico3d_filter_t)vals[ARG_filter].u_int;
    self->mat.double_sided = vals[ARG_double_sided].u_bool;
    self->mat.alpha_cutoff = (uint8_t)vals[ARG_alpha_cutoff].u_int;
    self->mat.specular  = (uint32_t)vals[ARG_specular].u_int;
    self->mat.shininess = vals[ARG_shininess].u_int;

    // fill a texture view from an image arg (returns false if the arg is None)
    auto from_image = [&](mp_obj_t o, pico3d_texture_t &tv) -> bool {
      if(o == mp_const_none) return false;
      if(!mp_obj_is_type(o, &type_image))
        mp_raise_msg(&mp_type_TypeError, MP_ERROR_TEXT("texture/normal_map/matcap must be an image"));
      image_obj_t *img = (image_obj_t *)MP_OBJ_TO_PTR(o);
      rect_t b = img->image->bounds();
      tv.texels = (const uint32_t *)img->image->ptr(0, 0);
      tv.width  = (int)b.w;
      tv.height = (int)b.h;
      return true;
    };

    if(from_image(vals[ARG_texture].u_obj, self->tex)) {
      self->mat.texture = &self->tex;
      self->texture_ref = vals[ARG_texture].u_obj;
    }
    if(from_image(vals[ARG_normal_map].u_obj, self->nmap)) {
      self->mat.normal_map = &self->nmap;
      self->normal_ref = vals[ARG_normal_map].u_obj;
    }
    if(from_image(vals[ARG_matcap].u_obj, self->mcap)) {
      self->mat.matcap = &self->mcap;
      self->matcap_ref = vals[ARG_matcap].u_obj;
    }
    return MP_OBJ_FROM_PTR(self);
  }

  MP_DEFINE_CONST_OBJ_TYPE(
      type_pico3d_material, MP_QSTR_material, MP_TYPE_FLAG_NONE,
      make_new, (const void *)pico3d_material_make_new);

  // ---- light ---------------------------------------------------------------

  static mp_obj_t pico3d_light_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_direction, ARG_color, ARG_ambient, ARG_position, ARG_atten };
    static const mp_arg_t allowed[] = {
      { MP_QSTR_direction, MP_ARG_OBJ, {.u_obj = mp_const_none} },
      { MP_QSTR_color,     MP_ARG_INT, {.u_int = 0xffffff} },
      { MP_QSTR_ambient,   MP_ARG_INT, {.u_int = 0x000000} },
      { MP_QSTR_position,  MP_ARG_OBJ, {.u_obj = mp_const_none} },  // set -> point light
      { MP_QSTR_atten,     MP_ARG_OBJ, {.u_obj = mp_const_none} },  // falloff k (float)
    };
    mp_arg_val_t vals[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed), allowed, vals);

    light_obj_t *self = mp_obj_malloc(light_obj_t, type);
    self->light.direction = (vals[ARG_direction].u_obj == mp_const_none)
                            ? vec3_t(0, 0, -1) : get_vec3(vals[ARG_direction].u_obj);
    self->light.color   = (uint32_t)vals[ARG_color].u_int;
    self->light.ambient = (uint32_t)vals[ARG_ambient].u_int;
    self->light.half    = vec3_t(0, 0, 0);
    if (vals[ARG_position].u_obj == mp_const_none) {
      self->light.point = 0; self->light.position = vec3_t(0, 0, 0); self->light.atten = 0.0f;
    } else {
      self->light.point = 1;
      self->light.position = get_vec3(vals[ARG_position].u_obj);
      self->light.atten = (vals[ARG_atten].u_obj == mp_const_none)
                          ? 1.0f : mp_obj_get_float(vals[ARG_atten].u_obj);
    }
    return MP_OBJ_FROM_PTR(self);
  }

  MP_DEFINE_CONST_OBJ_TYPE(
      type_pico3d_light, MP_QSTR_light, MP_TYPE_FLAG_NONE,
      make_new, (const void *)pico3d_light_make_new);

  // ---- target --------------------------------------------------------------

  // Fill a pico3d_target_t view from the wrapped image, (re)allocating the
  // depth buffer if the image has been resized since construction.
  static void target_view(target_obj_t *self, pico3d_target_t *t) {
    image_obj_t *img = (image_obj_t *)MP_OBJ_TO_PTR(self->image_ref);
    image_t *im = img->image;
    rect_t b  = im->bounds();
    rect_t cl = im->clip();
    int iw = (int)b.w, ih = (int)b.h;
    if(iw != self->w || ih != self->h) {
      self->depth = (uint16_t *)m_malloc(sizeof(uint16_t) * iw * ih);
      self->w = iw; self->h = ih;
    }
    t->color        = (uint32_t *)im->ptr(0, 0);
    t->depth        = self->depth;
    t->width        = iw;
    t->height       = ih;
    t->color_stride = im->row_stride() / im->bytes_per_pixel();
    t->depth_stride = self->w;
    t->clip_x0 = (int)cl.x;          t->clip_y0 = (int)cl.y;
    t->clip_x1 = (int)(cl.x + cl.w); t->clip_y1 = (int)(cl.y + cl.h);
  }

  static mp_obj_t pico3d_target_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    if(!mp_obj_is_type(args[0], &type_image))
      mp_raise_msg(&mp_type_TypeError, MP_ERROR_TEXT("target() expects an image"));
    image_obj_t *img = (image_obj_t *)MP_OBJ_TO_PTR(args[0]);
    if(img->image->pixel_format() != RGBA8888)
      mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("pico3d needs an RGBA8888 image"));

    target_obj_t *self = mp_obj_malloc(target_obj_t, type);
    self->image_ref = args[0];
    rect_t b = img->image->bounds();
    self->w = (int)b.w; self->h = (int)b.h;
    self->depth = m_new(uint16_t, self->w * self->h);
    self->vcache = nullptr; self->vcache_cap = 0;
    return MP_OBJ_FROM_PTR(self);
  }

  MPY_BIND_VAR(1, clear_depth, {
    target_obj_t *self = (target_obj_t *)MP_OBJ_TO_PTR(args[0]);
    uint16_t v = (n_args >= 2) ? (uint16_t)mp_obj_get_int(args[1]) : 0xFFFF;
    pico3d_target_t t{};
    t.depth = self->depth; t.width = self->w; t.height = self->h; t.depth_stride = self->w;
    pico3d_depth_clear(&t, v);
    return mp_const_none;
  })

  // render(mesh, model, view_proj, material[, light][, depth][, view])
  // depth (default True): pass False to skip the Z-buffer entirely for this call —
  // no per-pixel depth read/write (those go to PSRAM, so this is a big saving for
  // convex/back-face-culled meshes that don't need it, e.g. a globe).
  // view (optional, 7th arg): the camera/view matrix on its own (NOT view_proj) —
  // only used by matcap materials so the reflection tracks the camera.
  MPY_BIND_VAR(5, render, {
    target_obj_t *self = (target_obj_t *)MP_OBJ_TO_PTR(args[0]);
    if(!mp_obj_is_type(args[1], &type_pico3d_mesh) ||
       !mp_obj_is_type(args[2], &type_pico3d_mat4) ||
       !mp_obj_is_type(args[3], &type_pico3d_mat4) ||
       !mp_obj_is_type(args[4], &type_pico3d_material))
      mp_raise_msg(&mp_type_TypeError, MP_ERROR_TEXT("draw(mesh, model, view_proj, material, light=None, depth=True, view=None)"));

    mesh_obj_t     *mesh  = (mesh_obj_t *)MP_OBJ_TO_PTR(args[1]);
    mat4_obj_t     *model = (mat4_obj_t *)MP_OBJ_TO_PTR(args[2]);
    mat4_obj_t     *vp    = (mat4_obj_t *)MP_OBJ_TO_PTR(args[3]);
    material_obj_t *mat   = (material_obj_t *)MP_OBJ_TO_PTR(args[4]);

    pico3d_light_t *lp = nullptr;
    if(n_args >= 6 && args[5] != mp_const_none) {
      if(!mp_obj_is_type(args[5], &type_pico3d_light))
        mp_raise_msg(&mp_type_TypeError, MP_ERROR_TEXT("light must be a pico3d.light"));
      lp = &((light_obj_t *)MP_OBJ_TO_PTR(args[5]))->light;
    }

    const mat4_t *viewp = nullptr;
    if(n_args >= 8 && args[7] != mp_const_none) {
      if(!mp_obj_is_type(args[7], &type_pico3d_mat4))
        mp_raise_msg(&mp_type_TypeError, MP_ERROR_TEXT("view must be a pico3d.mat4"));
      viewp = &((mat4_obj_t *)MP_OBJ_TO_PTR(args[7]))->m;
    }

    // grow the per-vertex transform scratch to fit this mesh (reused each frame)
    if(mesh->mesh.vertex_count > self->vcache_cap) {
      self->vcache = m_new(pico3d_vcache_t, mesh->mesh.vertex_count);
      self->vcache_cap = mesh->mesh.vertex_count;
    }

    pico3d_target_t t;
    target_view(self, &t);
    // depth=False (7th arg): drop the Z-buffer for this call (no per-pixel depth
    // read/write — both hit PSRAM). emit() skips depth when t.depth is null.
    if(n_args >= 7 && !mp_obj_is_true(args[6]))
      t.depth = nullptr;
    int drawn = pico3d_draw_mesh(&t, &mesh->mesh, &model->m, &vp->m, &mat->mat, mat->shading, lp, self->vcache, viewp);
    return mp_obj_new_int(drawn);
  })

  MPY_BIND_LOCALS_DICT(target,
    MPY_BIND_ROM_PTR(clear_depth),
    MPY_BIND_ROM_PTR(render),
  )

  MP_DEFINE_CONST_OBJ_TYPE(
      type_pico3d_target, MP_QSTR_surface, MP_TYPE_FLAG_NONE,
      make_new, (const void *)pico3d_target_make_new,
      locals_dict, &target_locals_dict);

  // ---- module-level helpers ------------------------------------------------

  static mp_obj_t pico3d_perspective(size_t n_args, const mp_obj_t *args) {
    mat4_t m;
    m.perspective(mp_obj_get_float(args[0]), mp_obj_get_float(args[1]),
                  mp_obj_get_float(args[2]), mp_obj_get_float(args[3]));
    return make_mat4(m);
  }
  static MP_DEFINE_CONST_FUN_OBJ_VAR(pico3d_perspective_obj, 4, pico3d_perspective);

  static mp_obj_t pico3d_look_at(size_t n_args, const mp_obj_t *args) {
    mat4_t m;
    m.look_at(get_vec3(args[0]), get_vec3(args[1]), get_vec3(args[2]));
    return make_mat4(m);
  }
  static MP_DEFINE_CONST_FUN_OBJ_VAR(pico3d_look_at_obj, 3, pico3d_look_at);

  static mp_obj_t pico3d_make_rgb(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    return mp_obj_new_int((mp_int_t)picovector::pico3d_rgb(
        (uint8_t)mp_obj_get_int(args[0]),
        (uint8_t)mp_obj_get_int(args[1]),
        (uint8_t)mp_obj_get_int(args[2])));
  }
  static MP_DEFINE_CONST_FUN_OBJ_VAR(pico3d_rgb_obj, 3, pico3d_make_rgb);

  // cores(2) splits the rasterise pass across both cores; cores(1) uses one. Returns
  // the resulting core count (always 1 on host).
  static mp_obj_t pico3d_cores(size_t n_args, const mp_obj_t *args) {
    if (n_args) picovector::pico3d_set_cores((int)mp_obj_get_int(args[0]));
    return mp_obj_new_int(picovector::pico3d_get_cores());
  }
  static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pico3d_cores_obj, 0, 1, pico3d_cores);

  // TEMP phase profiler: returns (transform, build, project, planes, edges, fill,
  // bbox_px, covered_px) accumulated since the last call (cycle-accurate via DWT),
  // and resets. project/planes/edges are the 3-way split of the old setup phase.
  // Used by pico3d_bench. Values are 0 on host / non-instrumented builds.
  static mp_obj_t pico3d_prof(void) {
    mp_obj_t tup[8] = {
      mp_obj_new_int_from_uint((mp_uint_t)picovector::pico3d_prof_transform_cyc),
      mp_obj_new_int_from_uint((mp_uint_t)picovector::pico3d_prof_build_cyc),
      mp_obj_new_int_from_uint((mp_uint_t)picovector::pico3d_prof_project_cyc),
      mp_obj_new_int_from_uint((mp_uint_t)picovector::pico3d_prof_planes_cyc),
      mp_obj_new_int_from_uint((mp_uint_t)picovector::pico3d_prof_edges_cyc),
      mp_obj_new_int_from_uint((mp_uint_t)picovector::pico3d_prof_fill_cyc),
      mp_obj_new_int_from_uint((mp_uint_t)picovector::pico3d_prof_bbox_px),
      mp_obj_new_int_from_uint((mp_uint_t)picovector::pico3d_prof_px),
    };
    picovector::pico3d_prof_transform_cyc = 0;
    picovector::pico3d_prof_build_cyc = 0;
    picovector::pico3d_prof_project_cyc = 0;
    picovector::pico3d_prof_planes_cyc = 0;
    picovector::pico3d_prof_edges_cyc = 0;
    picovector::pico3d_prof_fill_cyc = 0;
    picovector::pico3d_prof_bbox_px = 0;
    picovector::pico3d_prof_px = 0;
    return mp_obj_new_tuple(8, tup);
  }
  static MP_DEFINE_CONST_FUN_OBJ_0(pico3d_prof_obj, pico3d_prof);

  // ---- module --------------------------------------------------------------

  static const mp_rom_map_elem_t pico3d_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),   MP_ROM_QSTR(MP_QSTR_pico3d) },
    { MP_ROM_QSTR(MP_QSTR_mat4),       MP_ROM_PTR(&type_pico3d_mat4) },
    { MP_ROM_QSTR(MP_QSTR_vec3),       MP_ROM_PTR(&type_pico3d_vec3) },
    { MP_ROM_QSTR(MP_QSTR_mesh),       MP_ROM_PTR(&type_pico3d_mesh) },
    { MP_ROM_QSTR(MP_QSTR_material),   MP_ROM_PTR(&type_pico3d_material) },
    { MP_ROM_QSTR(MP_QSTR_light),      MP_ROM_PTR(&type_pico3d_light) },
    { MP_ROM_QSTR(MP_QSTR_surface),    MP_ROM_PTR(&type_pico3d_target) },
    { MP_ROM_QSTR(MP_QSTR_perspective),MP_ROM_PTR(&pico3d_perspective_obj) },
    { MP_ROM_QSTR(MP_QSTR_look_at),    MP_ROM_PTR(&pico3d_look_at_obj) },
    { MP_ROM_QSTR(MP_QSTR_rgb),        MP_ROM_PTR(&pico3d_rgb_obj) },
    { MP_ROM_QSTR(MP_QSTR_cores),      MP_ROM_PTR(&pico3d_cores_obj) },
    { MP_ROM_QSTR(MP_QSTR_prof),       MP_ROM_PTR(&pico3d_prof_obj) },
    { MP_ROM_QSTR(MP_QSTR_FLAT),       MP_ROM_INT(PICO3D_FLAT) },
    { MP_ROM_QSTR(MP_QSTR_GOURAUD),    MP_ROM_INT(PICO3D_GOURAUD) },
    { MP_ROM_QSTR(MP_QSTR_UNLIT),      MP_ROM_INT(PICO3D_UNLIT) },
    { MP_ROM_QSTR(MP_QSTR_NEAREST),    MP_ROM_INT(PICO3D_NEAREST) },
    { MP_ROM_QSTR(MP_QSTR_BILINEAR),   MP_ROM_INT(PICO3D_BILINEAR) },
  };
  static MP_DEFINE_CONST_DICT(pico3d_globals, pico3d_globals_table);

  // C++ gives a file-scope `const` object internal linkage by default, but the
  // module table generated into objmodule.c references this symbol externally.
  // The explicit `extern` declaration forces external linkage (a .c module
  // wouldn't need this — const is external in C).
  extern const mp_obj_module_t pico3d_user_cmodule;
  const mp_obj_module_t pico3d_user_cmodule = {
    { &mp_type_module },                  // base (positional; designated init is C++20)
    (mp_obj_dict_t *)&pico3d_globals,     // globals
  };

  MP_REGISTER_MODULE(MP_QSTR_pico3d, pico3d_user_cmodule);

}
