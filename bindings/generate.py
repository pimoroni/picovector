#!/usr/bin/env python3
"""generate.py — turn the binding IR (built from the api/ stubs) into the
standard MicroPython C++ bindings for the PicoVector library.

Usage:
    python generate.py [--out DIR] [--list]

Emits one ``<type>.cpp`` per type plus ``types.h`` and
``picovector_bindings.c`` into ``--out`` (default: ``generated/``).  The
generated code uses only the lean helpers in ``runtime/pv_bindings.hpp`` — no
MPY_BIND_* macros.  ``runtime/pv_support.cpp`` and the ``native/*.cpp`` files
supply the shared glue and the few irreducibly-procedural bodies.
"""

from __future__ import annotations

import argparse
import os
import sys

import pv
import model
from model import (Returns, SELF, VOID, MUTATE, TUPLE2F, FILTER, F32, ANY)

HERE = os.path.dirname(os.path.abspath(__file__))


# ─────────────────────────────────────────────────────────────────────────────
# small text helpers
# ─────────────────────────────────────────────────────────────────────────────
class Out:
    def __init__(self):
        self.lines = []

    def __call__(self, s="", indent=0):
        self.lines.append(("  " * indent) + s if s else "")

    def raw(self, s):
        self.lines.append(s)

    def __str__(self):
        return "\n".join(self.lines) + "\n"


def fn_name(t, m):
    return f"mpy_{t.name}_{m.name}"


def native_name(t, m):
    return f"{t.name}_{m.name}"


def min_args(t, params, kind):
    base = 1 if kind == "instance" else 0
    return base + sum(0 if p.optional else 1 for p in params)


# ─────────────────────────────────────────────────────────────────────────────
# parameter parsing → C++ local declarations
# ─────────────────────────────────────────────────────────────────────────────
def _default_cpp(p):
    d = p.default
    if isinstance(d, pv._Same):
        return d.name
    if d is None:
        return "nullptr"
    if d is True:
        return "true"
    if d is False:
        return "false"
    if p.conv is FILTER:
        return "NEAREST"
    return repr(d)


def _range_checks(p, o, indent):
    r = p.range
    if not r:
        return
    n = p.name
    is_float = p.conv.cpp_type == "float"
    if r.clamp:
        if r.lo is not None:
            o(f"if ({n} < {r.lo}) {n} = {r.lo};", indent)
        if r.hi is not None:
            o(f"if ({n} > {r.hi}) {n} = {r.hi};", indent)
    else:
        conds = []
        if r.lo is not None:
            conds.append(f"{n} < {r.lo}")
        if r.hi is not None:
            conds.append(f"{n} > {r.hi}")
        o(f'if ({" || ".join(conds)}) mp_raise_msg_varg('
          f'&mp_type_ValueError, MP_ERROR_TEXT("{n} out of range"));', indent)


def emit_params(o, params, base, indent):
    """Emit local declarations for `params` using a running index `_i`.

    Returns the set of names declared (the call refers to them).  Handles
    composite (xy/xywh/stops/pattern8), optional and pointer-optional params,
    and the patched-in 'enough remaining args' check after composites.
    """
    if not params:
        return
    o(f"size_t _i = {base};", indent)
    saw_composite = False
    checked = False
    remaining = list(params)
    for k, p in enumerate(params):
        remaining = params[k:]
        c = p.conv
        # consolidated remaining-required check after any composite
        is_composite = c.kind in ("xy", "xywh", "stops", "pattern8")
        if saw_composite and not checked and not is_composite and not p.optional:
            rem = sum(0 if q.optional else 1 for q in remaining)
            o(f"pv::need(n_args, _i + {rem});", indent)
            checked = True

        if c.kind == "xy":
            o(f"vec2_t {p.name} = pv::get_xy(args, &_i, n_args);", indent)
            saw_composite = True
        elif c.kind == "xywh":
            o(f"rect_t {p.name} = pv::get_xywh(args, &_i, n_args);", indent)
            saw_composite = True
        elif c.kind == "stops":
            _emit_stops(o, p, indent)
            saw_composite = True
        elif c.kind == "pattern8":
            _emit_pattern8(o, p, indent)
            saw_composite = True
        elif p.optional and c.cpp_type.endswith("*"):
            chk = c.is_check("args[_i]")
            o(f"{c.cpp_type} {p.name} = nullptr;", indent)
            o(f"if (n_args > _i && {chk}) {{ {p.name} = {c.from_mp('args[_i]')}; _i++; }}", indent)
        elif p.optional:
            o(f"{c.cpp_type} {p.name} = {_default_cpp(p)};", indent)
            o(f"if (n_args > _i) {{ {p.name} = {c.from_mp('args[_i]')}; _i++; }}", indent)
            _range_checks(p, o, indent)
        else:
            o(f"{c.cpp_type} {p.name} = {c.from_mp('args[_i]')}; _i++;", indent)
            _range_checks(p, o, indent)


def _emit_stops(o, p, indent):
    n = p.name
    o(f"size_t {n}_n; mp_obj_t *{n}_items;", indent)
    o(f"mp_obj_get_array(args[_i], &{n}_n, &{n}_items); _i++;", indent)
    o(f"if ({n}_n < 1 || {n}_n > 16) mp_raise_msg_varg(&mp_type_ValueError, "
      f'MP_ERROR_TEXT("gradient expects 1 to 16 colour stops"));', indent)
    o(f"float {n}_positions[16]; uint32_t {n}_colors[16];", indent)
    o(f"for (size_t _s = 0; _s < {n}_n; _s++) {{", indent)
    o(f"size_t _sl; mp_obj_t *_stop; mp_obj_get_array({n}_items[_s], &_sl, &_stop);", indent + 1)
    o("if (_sl != 2 || !mp_obj_is_type(_stop[1], &type_color)) mp_raise_msg_varg("
      '&mp_type_TypeError, MP_ERROR_TEXT("each stop must be (position, color)"));', indent + 1)
    o(f"{n}_positions[_s] = mp_obj_get_float(_stop[0]);", indent + 1)
    o(f"{n}_colors[_s] = ((color_obj_t *)MP_OBJ_TO_PTR(_stop[1]))->c->_p;", indent + 1)
    o("}", indent)


def _emit_pattern8(o, p, indent):
    n = p.name
    o(f"size_t {n}_len; mp_obj_t *{n}_items;", indent)
    o(f"mp_obj_get_array(args[_i], &{n}_len, &{n}_items); _i++;", indent)
    o(f"if ({n}_len != 8) mp_raise_msg_varg(&mp_type_TypeError, "
      f'MP_ERROR_TEXT("custom pattern must be a tuple with 8 elements"));', indent)
    o(f"uint8_t {n}[8];", indent)
    o(f"for (int _b = 0; _b < 8; _b++) {n}[_b] = mp_obj_get_int({n}_items[_b]);", indent)


# ─────────────────────────────────────────────────────────────────────────────
# call expression + return
# ─────────────────────────────────────────────────────────────────────────────
def call_expr(t, call, args, emit, recv, params):
    arglist = args if args is not None else tuple(p.name for p in params)
    a = ", ".join(arglist)
    if recv:
        return f"{recv}->{call}({a})"
    if emit == "method":
        op = "->" if t.field_is_ptr else "."
        return f"self->{t.field}{op}{call}({a})"
    if emit == "free":
        return f"{call}({a})"
    if emit == "new":
        return f"new {call}({a})"
    if emit == "mnew":
        return f"m_new_class({call}, {a})"
    if emit == "expr":
        return a
    raise ValueError(f"unknown emit {emit!r}")


def emit_return(o, t, returns, ce, box, indent):
    if returns is VOID:
        o(f"{ce};", indent)
        o("return mp_const_none;", indent)
    elif returns is SELF:
        o(f"{ce};", indent)
        o("return MP_OBJ_FROM_PTR(self);", indent)
    elif returns is MUTATE:
        o(f"self->{t.field} = {ce};", indent)
        o("return mp_const_none;", indent)
    elif isinstance(returns, Returns):
        expr = box.format(ce) if box else returns.conv.to_mp(ce)
        o(f"return {expr};", indent)
    else:
        raise ValueError(f"unhandled return {returns!r}")


# ─────────────────────────────────────────────────────────────────────────────
# member function
# ─────────────────────────────────────────────────────────────────────────────
def emit_member(o, t, m):
    if m.native:
        return  # body provided by native/*.cpp; only the obj is declared later

    o(f"// {t.name}.{m.name}: {m.doc}")
    # non-static: unique mpy_<type>_<name> symbols, callable from native/*.cpp
    # (e.g. image.batch dispatches to the generated drawing functions).
    o(f"mp_obj_t {fn_name(t, m)}(size_t n_args, const mp_obj_t *args) {{")
    if m.kind == "instance":
        o(f"self(args[0], {t.obj_struct});", 1)

    # single special-case bodies -------------------------------------------
    if len(m.params) == 1 and m.params[0].conv.kind == "pathlist":
        _emit_pathlist_body(o, t, m)
        o("}")
        o("")
        return
    if len(m.params) == 1 and m.params[0].conv.kind == "shapelist":
        _emit_shapelist_body(o, t, m)
        o("}")
        o("")
        return

    if m.overloads:
        for ov in m.overloads:
            # n_args check first so it short-circuits before any args[i] read
            guard = [f"n_args {ov.nargs}"] if ov.nargs else []
            guard += [c.is_check(f"args[{i}]") for i, c in ov.guard]
            cond = " && ".join(guard) if guard else "true"
            o(f"if ({cond}) {{", 1)
            emit_params(o, ov.params, 1 if m.kind == "instance" else 0, 2)
            ce = call_expr(t, ov.call, ov.args, ov.emit, ov.recv, ov.params)
            emit_return(o, t, ov.returns, ce, ov.box, 2)
            o("}", 1)
        o(f'mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("{m.error}"));', 1)
    else:
        emit_params(o, m.params, 1 if m.kind == "instance" else 0, 1)
        ce = call_expr(t, m.call, m.args, m.emit, m.recv, m.params)
        emit_return(o, t, m.returns, ce, m.box, 1)
    o("}")
    o("")


def _emit_pathlist_body(o, t, m):
    name = m.params[0].name + "_shape"
    o(f"shape_t *{name} = new (PV_MALLOC(sizeof(shape_t))) shape_t(n_args);", 1)
    o("for (size_t _p = 0; _p < n_args; _p++) {", 1)
    o("if (!mp_obj_is_type(args[_p], &mp_type_list)) mp_raise_msg_varg("
      '&mp_type_TypeError, MP_ERROR_TEXT("expected a list of vec2 points"));', 2)
    o("size_t _pc; mp_obj_t *_pts; mp_obj_list_get(args[_p], &_pc, &_pts);", 2)
    o("path_t poly(_pc);", 2)
    o("for (size_t _k = 0; _k < _pc; _k++) {", 2)
    o("if (!mp_obj_is_type(_pts[_k], &type_vec2)) mp_raise_msg_varg("
      '&mp_type_TypeError, MP_ERROR_TEXT("expected a list of vec2 points"));', 3)
    o("poly.add_point(((vec2_obj_t *)MP_OBJ_TO_PTR(_pts[_k]))->v);", 3)
    o("}", 2)
    o(f"{name}->add_path(poly);", 2)
    o("}", 1)
    o(f"return pv::box_shape({name});", 1)


def _emit_shapelist_body(o, t, m):
    o("mp_obj_t _arg = args[1];", 1)
    o("if (mp_obj_is_type(_arg, &type_shape)) {", 1)
    o(f"self->{t.field}->{m.call}(((shape_obj_t *)MP_OBJ_TO_PTR(_arg))->shape);", 2)
    o("return mp_const_none;", 2)
    o("}", 1)
    o("if (mp_obj_is_type(_arg, &mp_type_list)) {", 1)
    o("size_t _len; mp_obj_t *_items; mp_obj_list_get(_arg, &_len, &_items);", 2)
    o(f"for (size_t _k = 0; _k < _len; _k++) self->{t.field}->{m.call}("
      "((shape_obj_t *)MP_OBJ_TO_PTR(_items[_k]))->shape);", 2)
    o("return mp_const_none;", 2)
    o("}", 1)
    o('mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT('
      '"expected a shape or a list of shapes"));', 1)


def emit_member_obj(o, t, m):
    """The MP_DEFINE_CONST_FUN_OBJ_* (+ staticmethod wrapper) for a member."""
    fn = native_name(t, m) if m.native else fn_name(t, m)
    obj = f"{fn_name(t, m)}_obj"
    n = min_args(t, m.params, m.kind)
    if m.native:
        o(f"extern \"C\" mp_obj_t {fn}(size_t n_args, const mp_obj_t *args);")
    o(f"static MP_DEFINE_CONST_FUN_OBJ_VAR({obj}, {n}, {fn});")
    if m.is_static:
        o(f"static MP_DEFINE_CONST_STATICMETHOD_OBJ({fn_name(t, m)}_static_obj, "
          f"MP_ROM_PTR(&{obj}));")


# ─────────────────────────────────────────────────────────────────────────────
# make_new
# ─────────────────────────────────────────────────────────────────────────────
def emit_make_new(o, t):
    mk = t.make_new
    o(f"static mp_obj_t {t.name}_make_new(const mp_obj_type_t *type, "
      "size_t n_args, size_t n_kw, const mp_obj_t *args) {")
    alloc = "mp_obj_malloc_with_finaliser" if mk.finaliser else "mp_obj_malloc"
    o(f"{t.obj_struct} *self = {alloc}({t.obj_struct}, type);", 1)
    if mk.kind == "image":
        o("int w = mp_obj_get_int(args[0]);", 1)
        o("int h = mp_obj_get_int(args[1]);", 1)
        o("if (n_args > 2) {", 1)
        o("mp_buffer_info_t bufinfo;", 2)
        o("mp_get_buffer_raise(args[2], &bufinfo, MP_BUFFER_WRITE);", 2)
        o("self->image = new (m_malloc(sizeof(image_t))) image_t(bufinfo.buf, w, h);", 2)
        o("} else {", 1)
        o("self->image = new (m_malloc(sizeof(image_t))) image_t(w, h);", 2)
        o("}", 1)
    elif len(mk.variants) == 1:
        for lhs, rhs in mk.variants[0].assigns:
            o(f"{lhs} = {rhs};", 1)
    else:
        first = True
        for v in mk.variants:
            kw = "if" if first else "else if"
            first = False
            o(f"{kw} (n_args == {v.nargs}) {{", 1)
            for lhs, rhs in v.assigns:
                o(f"{lhs} = {rhs};", 2)
            o("}", 1)
        o(f'else mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("{mk.error}"));', 1)
    o("return MP_OBJ_FROM_PTR(self);", 1)
    o("}")
    o("")


# ─────────────────────────────────────────────────────────────────────────────
# __del__
# ─────────────────────────────────────────────────────────────────────────────
def emit_del(o, t):
    if t.del_native:
        o(f'extern "C" mp_obj_t {t.name}__del__(mp_obj_t self_in);')
    else:
        o(f"static mp_obj_t {t.name}__del__(mp_obj_t self_in) {{")
        o(f"self(self_in, {t.obj_struct});", 1)
        o(f"{t.del_stmt};", 1)
        o("return mp_const_none;", 1)
        o("}")
    o(f"static MP_DEFINE_CONST_FUN_OBJ_1({t.name}__del___obj, {t.name}__del__);")
    o("")


# ─────────────────────────────────────────────────────────────────────────────
# attr (properties)
# ─────────────────────────────────────────────────────────────────────────────
def emit_attr(o, t):
    # non-static so native/image.cpp (batch) can reuse image_attr for setters
    o(f"void {t.name}_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {{")
    o(f"self(self_in, {t.obj_struct});", 1)
    o("action_t action = m_attr_action(dest);", 1)
    o("switch (attr) {", 1)
    for p in t.props:
        for q in (p.name,) + tuple(p.aliases):
            o(f"case MP_QSTR_{q}:", 2)
        o("{", 2)
        if p.kind == "pen":
            _emit_pen(o)
        elif p.kind == "font":
            _emit_font(o)
        else:
            get = p.get_raw if p.get_raw else p.conv.to_mp(p.get)
            o(f"if (action == GET) {{ dest[0] = {get}; return; }}", 3)
            if p.writable:
                if "{0}" in p.set:
                    stmt = p.set.format(p.conv.from_mp("dest[1]"))
                else:
                    stmt = f"{p.set} = {p.conv.from_mp('dest[1]')}"
                o(f"if (action == SET) {{ {stmt}; dest[0] = MP_OBJ_NULL; return; }}", 3)
        o("}", 2)
    o("}", 1)
    o("dest[1] = MP_OBJ_SENTINEL;", 1)
    o("}")
    o("")


def _emit_pen(o):
    o("if (action == GET) { dest[0] = self->brush ? MP_OBJ_FROM_PTR(self->brush) "
      ": mp_const_none; return; }", 3)
    o("if (action == SET) {", 3)
    o("brush_obj_t *brush = mp_obj_to_brush(1, &dest[1]);", 4)
    o('if (!brush) mp_raise_TypeError(MP_ERROR_TEXT("value must be of type brush or color"));', 4)
    o("self->brush = brush; self->image->brush(brush->brush);", 4)
    o("dest[0] = MP_OBJ_NULL; return;", 4)
    o("}", 3)


def _emit_font(o):
    o("if (action == GET) {", 3)
    o("if (self->font) dest[0] = MP_OBJ_FROM_PTR(self->font);", 4)
    o("else if (self->pixel_font) dest[0] = MP_OBJ_FROM_PTR(self->pixel_font);", 4)
    o("else dest[0] = mp_const_none;", 4)
    o("return;", 4)
    o("}", 3)
    o("if (action == SET) {", 3)
    o("if (mp_obj_is_type(dest[1], &type_font)) {", 4)
    o("self->font = (font_obj_t *)dest[1]; self->pixel_font = nullptr;", 5)
    o("self->image->font(&self->font->font);", 5)
    o("} else if (mp_obj_is_type(dest[1], &type_pixel_font)) {", 4)
    o("self->pixel_font = (pixel_font_obj_t *)dest[1]; self->font = nullptr;", 5)
    o("self->image->pixel_font(self->pixel_font->font);", 5)
    o("} else {", 4)
    o('mp_raise_TypeError(MP_ERROR_TEXT("value must be of type Font or PixelFont"));', 5)
    o("}", 4)
    o("dest[0] = MP_OBJ_NULL; return;", 4)
    o("}", 3)


# ─────────────────────────────────────────────────────────────────────────────
# binary_op
# ─────────────────────────────────────────────────────────────────────────────
def emit_binary_op(o, t):
    o(f"static mp_obj_t {t.name}_binary_op(mp_binary_op_t op, mp_obj_t lhs_in, "
      "mp_obj_t rhs_in) {")
    o(f"{t.obj_struct} *lhs = ({t.obj_struct} *)MP_OBJ_TO_PTR(lhs_in);", 1)
    o("switch (op) {", 1)
    for b in t.binops:
        o(f"case MP_BINARY_OP_{b.op}: {{", 2)
        for conv, result in b.cases:
            if conv is F32:
                o("if (mp_obj_is_int(rhs_in) || mp_obj_is_float(rhs_in)) {", 3)
                o("float v = mp_obj_get_float(rhs_in);", 4)
                o(f"return {result};", 4)
                o("}", 3)
            else:
                o(f"if (mp_obj_is_type(rhs_in, &{conv.name and 'type_' + conv.name})) {{", 3)
                o(f"{t.obj_struct} *rhs = ({t.obj_struct} *)MP_OBJ_TO_PTR(rhs_in);", 4)
                o(f"return {result};", 4)
                o("}", 3)
        if b.default != "MP_OBJ_NULL":
            o(f"return {b.default};", 3)
        o("} break;", 2)
    o("default: break;  // unhandled ops fall through to MP_OBJ_NULL", 2)
    o("}", 1)
    o("return MP_OBJ_NULL;", 1)
    o("}")
    o("")


# ─────────────────────────────────────────────────────────────────────────────
# print + buffer
# ─────────────────────────────────────────────────────────────────────────────
def emit_print(o, t):
    o(f"static void {t.name}_print(const mp_print_t *print, mp_obj_t self_in, "
      "mp_print_kind_t kind) {")
    if t.print_args:
        o(f"self(self_in, {t.obj_struct});", 1)
    args = "".join(", " + a for a in t.print_args)
    o(f'mp_printf(print, "{t.print_fmt}"{args});', 1)
    o("}")
    o("")


def emit_buffer(o, t):
    o(f"static mp_int_t {t.name}_get_buffer(mp_obj_t self_in, "
      "mp_buffer_info_t *bufinfo, mp_uint_t flags) {")
    o(f"self(self_in, {t.obj_struct});", 1)
    o("bufinfo->buf = self->image->ptr(0, 0);", 1)
    o("bufinfo->len = self->image->buffer_size();", 1)
    o("bufinfo->typecode = 'B';", 1)
    o("return 0;", 1)
    o("}")
    o("")


# ─────────────────────────────────────────────────────────────────────────────
# palette (colour) static data
# ─────────────────────────────────────────────────────────────────────────────
def emit_palette(o, t):
    def rgba(v):
        return ", ".join(f"0x{c:02x}" for c in v)
    for pc in t.palette:
        var = f"_color_{pc.name}"
        if pc.tufty:
            o("#ifdef TUFTY")
            o(f"static rgb_color_t {var}({rgba(pc.tufty)});")
            o("#else")
            o(f"static rgb_color_t {var}({rgba(pc.rgba)});")
            o("#endif")
        else:
            o(f"static rgb_color_t {var}({rgba(pc.rgba)});")
    o("")
    for pc in t.palette:
        o(f"static const color_obj_t color_{pc.name}_obj = "
          f"{{.base = {{.type = &type_color}}, .c = &_color_{pc.name}}};")
    o("")


# ─────────────────────────────────────────────────────────────────────────────
# locals dict + type
# ─────────────────────────────────────────────────────────────────────────────
def emit_locals(o, t):
    o(f"static const mp_rom_map_elem_t {t.name}_locals_dict_table[] = {{")
    if t.has_del:
        o(f"{{ MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&{t.name}__del___obj) }},", 1)
    for c in t.consts:
        o(f"{{ MP_ROM_QSTR(MP_QSTR_{c.name}), MP_ROM_INT({c.value}) }},", 1)
    for pc in t.palette:
        o(f"{{ MP_ROM_QSTR(MP_QSTR_{pc.name}), MP_ROM_PTR(&color_{pc.name}_obj) }},", 1)
    for m in t.members:
        obj = f"{fn_name(t, m)}_static_obj" if m.is_static else f"{fn_name(t, m)}_obj"
        o(f"{{ MP_ROM_QSTR(MP_QSTR_{m.name}), MP_ROM_PTR(&{obj}) }},", 1)
    o("};")
    o(f"static MP_DEFINE_CONST_DICT({t.name}_locals_dict, {t.name}_locals_dict_table);")
    o("")


def emit_type(o, t):
    o(f"MP_DEFINE_CONST_OBJ_TYPE(")
    o(f"{t.mp_type},", 1)
    o(f"MP_QSTR_{t.name},", 1)
    o("MP_TYPE_FLAG_NONE,", 1)
    if t.has_make_new:
        o(f"make_new, (const void *){t.name}_make_new,", 1)
    if t.has_print:
        o(f"print, (const void *){t.name}_print,", 1)
    if t.has_binary_op:
        o(f"binary_op, (const void *){t.name}_binary_op,", 1)
    if t.has_attr:
        o(f"attr, (const void *){t.name}_attr,", 1)
    if t.has_buffer:
        o(f"buffer, (const void *){t.name}_get_buffer,", 1)
    o(f"locals_dict, &{t.name}_locals_dict", 1)
    o(");")
    o("")


# ─────────────────────────────────────────────────────────────────────────────
# whole-file assembly
# ─────────────────────────────────────────────────────────────────────────────
def emit_type_file(t):
    o = Out()
    o(f"// AUTO-GENERATED from api/{t.name}.py by generate.py — do not edit by hand.")
    o('#include "pv_bindings.hpp"')
    o('#include "types.h"')
    for inc in t.includes:
        o(f'#include "{inc}"')
    o("")
    o('extern "C" {')
    o("")
    if t.palette:
        emit_palette(o, t)
    for m in t.members:
        emit_member(o, t, m)
    for m in t.members:
        emit_member_obj(o, t, m)
    o("")
    if t.has_make_new:
        emit_make_new(o, t)
    if t.has_del:
        emit_del(o, t)
    if t.has_binary_op:
        emit_binary_op(o, t)
    if t.has_print:
        emit_print(o, t)
    if t.has_attr:
        emit_attr(o, t)
    if t.has_buffer:
        emit_buffer(o, t)
    emit_locals(o, t)
    emit_type(o, t)
    o("}")
    return str(o)


def emit_types_h(types):
    o = Out()
    o("// AUTO-GENERATED by generate.py — do not edit by hand.")
    o("#pragma once")
    o('#include "py/runtime.h"')
    o("")
    for t in types:
        o(f"extern const mp_obj_type_t {t.mp_type};")
    return str(o)


def emit_bindings_c(types):
    o = Out()
    o("// AUTO-GENERATED by generate.py — do not edit by hand.")
    o('#include "py/runtime.h"')
    o('#include "types.h"')
    o("")
    o("extern mp_obj_t modpicovector___init__(void);")
    o("static MP_DEFINE_CONST_FUN_OBJ_0(modpicovector___init___obj, "
      "modpicovector___init__);")
    o("")
    o("static const mp_rom_map_elem_t modpicovector_globals_table[] = {")
    o("{ MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_modpicovector) },", 1)
    o("{ MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&modpicovector___init___obj) },", 1)
    for t in types:
        o(f"{{ MP_ROM_QSTR(MP_QSTR_{t.name}), MP_ROM_PTR(&{t.mp_type}) }},", 1)
    o("};")
    o("static MP_DEFINE_CONST_DICT(modpicovector_globals, "
      "modpicovector_globals_table);")
    o("")
    o("const mp_obj_module_t modpicovector = {")
    o(".base = { &mp_type_module },", 1)
    o(".globals = (mp_obj_dict_t *)&modpicovector_globals,", 1)
    o("};")
    o("")
    o("MP_REGISTER_MODULE(MP_QSTR_picovector, modpicovector);")
    return str(o)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(HERE, "generated"))
    ap.add_argument("--list", action="store_true", help="list the surface and exit")
    args = ap.parse_args()

    types = model.load()

    if args.list:
        for t in types:
            print(t.name, "::",
                  ", ".join(m.name + ("()" if not m.is_static else "[static]")
                            for m in t.members))
        return

    os.makedirs(args.out, exist_ok=True)
    written = []
    for t in types:
        path = os.path.join(args.out, f"{t.name}.cpp")
        with open(path, "w") as f:
            f.write(emit_type_file(t))
        written.append(path)
    with open(os.path.join(args.out, "types.h"), "w") as f:
        f.write(emit_types_h(types))
    with open(os.path.join(args.out, "picovector_bindings.c"), "w") as f:
        f.write(emit_bindings_c(types))

    print(f"generated {len(written)} type sources + types.h + "
          f"picovector_bindings.c into {args.out}")


if __name__ == "__main__":
    main()
