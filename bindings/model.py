"""model.py — internal IR for the PicoVector bindings, and the loader that
builds it from the type-annotated stubs under ``api/``.

The stubs (authored against :mod:`pv`) are the human-facing source of truth.
:func:`load` introspects them — real signatures, annotations, docstrings and the
thin ``@cpp`` metadata — and produces the small dataclass IR below, which
``generate.py`` turns into C++.  Keeping a distinct IR means the generator never
has to know about Python introspection, and the stubs never contain C++.
"""

from __future__ import annotations

import inspect
import typing
from dataclasses import dataclass, field as dc_field
from typing import Optional

import pv


# ─────────────────────────────────────────────────────────────────────────────
# Converters: how one value crosses the C/Python boundary.
# ─────────────────────────────────────────────────────────────────────────────
@dataclass
class Converter:
    name: str
    cpp_type: str
    _from_mp: Optional[str] = None
    _to_mp: Optional[str] = None
    _is_check: Optional[str] = None
    consumes: int = 1          # positional args eaten (composites: see `kind`)
    kind: str = ""             # '' = simple; else xy/xywh/stops/pattern8/...

    def from_mp(self, expr):
        return self._from_mp.format(expr)

    def to_mp(self, expr):
        return self._to_mp.format(expr)

    def is_check(self, expr):
        return self._is_check.format(expr) if self._is_check else None


F32 = Converter("f32", "float", "mp_obj_get_float({0})", "mp_obj_new_float({0})",
                "(mp_obj_is_float({0}) || mp_obj_is_int({0}))")
# `int` params are read leniently via (int)mp_obj_get_float so a float-valued
# argument (e.g. color.rgb(0, 0, 0, 100 * pulse)) is accepted and truncated —
# matching the original bindings, which read most numeric args as floats. The
# type *check* stays strict (mp_obj_is_int) so overload dispatch can still tell
# an int index from a tuple/other type.
INT = Converter("int", "int", "(int)mp_obj_get_float({0})", "mp_obj_new_int({0})",
                "mp_obj_is_int({0})")
BOOL = Converter("bool", "bool", "mp_obj_is_true({0})", "mp_obj_new_bool({0})",
                 "mp_obj_is_bool({0})")
STR = Converter("str", "const char *", "mp_obj_str_get_str({0})", None,
                "mp_obj_is_str({0})")
FILTER = Converter("filter", "filter_t", "(filter_t)mp_obj_get_int({0})",
                   "mp_obj_new_int({0})", "mp_obj_is_int({0})")
BUFFER = Converter("buffer", "mp_buffer_info_t", None, None, None)
ANY = Converter("any", "mp_obj_t", "{0}", None, None)
TUPLE = Converter("tuple", "", None, None, "mp_obj_is_type({0}, &mp_type_tuple)")
LIST = Converter("list", "", None, None, "mp_obj_is_type({0}, &mp_type_list)")

# composite converters — consume a variable number of positional args
XY = Converter("xy", "vec2_t", consumes=2, kind="xy",
               _is_check="mp_obj_is_vec2({0})")
XYWH = Converter("xywh", "rect_t", consumes=4, kind="xywh",
                 _is_check="mp_obj_is_rect({0})")
STOPS = Converter("stops", "", consumes=1, kind="stops", _is_check="mp_obj_is_type({0}, &mp_type_list)")
PATTERN8 = Converter("pattern8", "", consumes=1, kind="pattern8",
                     _is_check="mp_obj_is_type({0}, &mp_type_tuple)")
PATHLIST = Converter("pathlist", "", consumes=1, kind="pathlist")
SHAPELIST = Converter("shapelist", "", consumes=1, kind="shapelist")

_PSEUDO = {
    "XY": XY, "XYWH": XYWH, "ColorStops": STOPS, "Pattern8": PATTERN8,
    "PathList": PATHLIST, "ShapeOrList": SHAPELIST, "Filter": FILTER,
    "Buffer": BUFFER,
}


# ─────────────────────────────────────────────────────────────────────────────
# Return specifications
# ─────────────────────────────────────────────────────────────────────────────
class _Ret:
    pass


@dataclass
class Returns(_Ret):
    conv: Converter


SELF = type("SELF", (_Ret,), {})()       # call; return MP_OBJ_FROM_PTR(self)
VOID = type("VOID", (_Ret,), {})()        # call; return mp_const_none
MUTATE = type("MUTATE", (_Ret,), {})()    # self->FIELD = call; return none
TUPLE2F = type("TUPLE2F", (_Ret,), {})()  # (float, float) tuple


# ─────────────────────────────────────────────────────────────────────────────
# IR dataclasses
# ─────────────────────────────────────────────────────────────────────────────
_NO_DEFAULT = object()


@dataclass
class Param:
    name: str
    conv: Converter
    default: object = _NO_DEFAULT
    range: Optional[pv.Range] = None

    @property
    def optional(self):
        return self.default is not _NO_DEFAULT


@dataclass
class Overload:
    guard: tuple = ()            # ((arg_index, Converter), …) → type tests
    nargs: str = ""              # extra "n_args <cond>"
    params: tuple = ()
    call: str = ""
    args: Optional[tuple] = None
    returns: _Ret = VOID
    emit: str = "method"
    recv: str = ""               # receive method-call on this param's local
    box: str = ""                # custom result box ({0} = call expr)


@dataclass
class Method:
    name: str
    kind: str                    # 'instance' | 'static'
    params: tuple = ()
    returns: _Ret = VOID
    doc: str = ""
    call: str = ""
    args: Optional[tuple] = None
    emit: str = "method"         # method | free | new | mnew | expr
    native: bool = False
    overloads: tuple = ()
    error: str = "invalid parameters"
    recv: str = ""               # receive method-call on this param's local
    box: str = ""                # custom result box ({0} = call expr)

    @property
    def is_static(self):
        return self.kind == "static"


@dataclass
class Prop:
    name: str
    conv: Optional[Converter] = None
    get: str = ""
    set: str = ""
    get_raw: str = ""
    aliases: tuple = ()
    kind: str = ""               # '' | 'pen' | 'font'
    doc: str = ""

    @property
    def writable(self):
        return bool(self.set) or self.kind in ("pen", "font")


@dataclass
class Const:
    name: str
    value: str
    doc: str = ""
    is_ptr: bool = False


@dataclass
class NewVariant:
    nargs: int
    assigns: tuple = ()
    placement: str = ""


@dataclass
class New:
    variants: tuple = ()
    doc: str = ""
    finaliser: bool = False
    error: str = "invalid parameters"
    kind: str = ""               # '' generic | 'image'


@dataclass
class BinOp:
    op: str
    cases: tuple = ()            # ((Converter|None, mp_expr), …)
    default: str = "MP_OBJ_NULL"
    doc: str = ""


@dataclass
class PaletteColor:
    name: str
    rgba: tuple
    tufty: Optional[tuple] = None
    doc: str = ""


@dataclass
class Type:
    name: str
    mp_type: str
    obj_struct: str
    cpp_class: str
    field: str
    field_is_ptr: bool
    arg_read: str
    arg_type: str
    box: Optional[str]
    has_attr: bool = False
    has_print: bool = False
    has_make_new: bool = False
    has_buffer: bool = False
    has_del: bool = False
    has_binary_op: bool = False
    del_stmt: str = ""
    del_native: bool = False
    includes: tuple = ()
    print_fmt: Optional[str] = None
    print_args: tuple = ()
    members: list = dc_field(default_factory=list)
    props: list = dc_field(default_factory=list)
    consts: list = dc_field(default_factory=list)
    binops: list = dc_field(default_factory=list)
    palette: list = dc_field(default_factory=list)
    make_new: Optional[New] = None

    def converter(self) -> Converter:
        return Converter(self.name, self.arg_type, self.arg_read,
                         self.box or None, f"mp_obj_is_type({{0}}, &{self.mp_type})")


CONVERTERS: dict[str, Converter] = {}


def ref(name) -> Converter:
    return CONVERTERS[name]


# ─────────────────────────────────────────────────────────────────────────────
# Loader
# ─────────────────────────────────────────────────────────────────────────────
_BINOPS = {
    "__add__": ("ADD", "+"), "__sub__": ("SUBTRACT", "-"),
    "__mul__": ("MULTIPLY", "*"), "__truediv__": ("TRUE_DIVIDE", "/"),
    "__eq__": ("EQUAL", "=="), "__ne__": ("NOT_EQUAL", "!="),
}


def _eval_ann(raw, ns):
    """Evaluate a (string) annotation in the shared namespace."""
    if isinstance(raw, str):
        return eval(raw, ns)
    return raw


def _split_annotated(ann):
    """(base type, Range|None) for an Annotated[...] (or (ann, None))."""
    if typing.get_origin(ann) is typing.Annotated:
        base, *meta = typing.get_args(ann)
        rng = next((m for m in meta if isinstance(m, pv.Range)), None)
        return base, rng
    return ann, None


def _ptr_conv(base: Converter) -> Converter:
    """Wrap an object converter as 'pointer to it, or nullptr when absent'."""
    return Converter(base.name + "*", base.cpp_type.rstrip(" *") + " *",
                     "&" + base._from_mp, base._to_mp, base._is_check)


def _conv_for(ann, ns, classes):
    """Resolve a *parameter* annotation to a Converter (+ optional Range)."""
    ann, rng = _split_annotated(ann)
    # Optional[T] / T | None  → pointer to T (nullptr default)
    origin = typing.get_origin(ann)
    if origin in (typing.Union, getattr(__import__("types"), "UnionType", ())):
        args = [a for a in typing.get_args(ann) if a is not type(None)]
        if len(args) == 1:
            base, _ = _conv_for(args[0], ns, classes)
            return _ptr_conv(base), rng
    if ann is float:
        return F32, rng
    if ann is int:
        return INT, rng
    if ann is bool:
        return BOOL, rng
    if ann is str:
        return STR, rng
    if isinstance(ann, pv._Pseudo):
        return _PSEUDO[ann.name], rng
    if ann in classes:
        return CONVERTERS[ann.__name__], rng
    raise TypeError(f"cannot map parameter annotation {ann!r}")


def _ret_for(ann, ns, classes, owner):
    """Resolve a *return* annotation to a return spec."""
    if ann is None or ann is type(None):
        return VOID
    if ann is typing.Self:
        return SELF
    if ann is float:
        return Returns(F32)
    if ann is int:
        return Returns(INT)
    if ann is bool:
        return Returns(BOOL)
    if ann is str:
        return Returns(STR)
    if typing.get_origin(ann) is tuple:
        return TUPLE2F
    if ann in classes:
        return Returns(CONVERTERS[ann.__name__])
    raise TypeError(f"cannot map return annotation {ann!r} on {owner}")


def _params(func, ns, classes, skip_self):
    """Build Param list from a function's signature + annotations."""
    sig = inspect.signature(func)
    anns = getattr(func, "__annotations__", {})
    out = []
    items = list(sig.parameters.items())
    if skip_self and items and items[0][0] == "self":
        items = items[1:]
    for pname, p in items:
        if pname == "return":
            continue
        if p.kind in (inspect.Parameter.VAR_POSITIONAL, inspect.Parameter.VAR_KEYWORD):
            continue
        if pname in anns:
            conv, rng = _conv_for(_eval_ann(anns[pname], ns), ns, classes)
        else:
            conv, rng = ANY, None      # unannotated (native bodies); count only
        default = _NO_DEFAULT if p.default is inspect.Parameter.empty else p.default
        out.append(Param(pname, conv, default, rng))
    return out


def _cppmeta(func):
    return dict(getattr(pv._unwrap(func), "__pv_cpp__", {}))


def _default_args(params):
    return tuple(p.name for p in params)


def _build_method(name, func, kind, ns, classes, owner):
    meta = _cppmeta(func)
    anns = getattr(func, "__annotations__", {})
    ret_ann = _eval_ann(anns["return"], ns) if "return" in anns else None
    overloads = typing.get_overloads(func)

    if overloads:
        ovs = []
        first_ret = VOID
        for ov in overloads:
            om = {**meta, **_cppmeta(ov)}   # impl @cpp = shared defaults
            ps = _params(ov, ns, classes, skip_self=(kind == "instance"))
            oret = _ret_for(_eval_ann(ov.__annotations__.get("return", None), ns),
                            ns, classes, owner)
            first_ret = oret
            guard, nargs = _overload_dispatch(om, ps, kind)
            ovs.append(Overload(
                guard=guard, nargs=nargs, params=tuple(ps),
                call=om.get("call", name), args=_args_of(om),
                returns=oret, emit=om.get("emit", _default_emit(kind)),
                recv=om.get("recv", ""), box=om.get("box", ""),
            ))
        return Method(name, kind, returns=first_ret, doc=(func.__doc__ or "").strip(),
                      overloads=tuple(ovs), emit=_default_emit(kind),
                      error=meta.get("error", "invalid parameters"))

    params = _params(func, ns, classes, skip_self=(kind == "instance"))
    returns = VOID if meta.get("native") else _ret_for(ret_ann, ns, classes, owner)
    emit = meta.get("emit", _default_emit(kind))
    if meta.get("emit") == "mutate":
        returns = MUTATE
        emit = "method"
    return Method(
        name, kind, params=tuple(params), returns=returns,
        doc=(func.__doc__ or "").strip(), call=meta.get("call", name),
        args=_args_of(meta), emit=emit, native=meta.get("native", False),
        error=meta.get("error", "invalid parameters"),
        recv=meta.get("recv", ""), box=meta.get("box", ""),
    )


def _args_of(meta):
    a = meta.get("args")
    if a is None:
        return None
    return tuple(a.split()) if isinstance(a, str) else tuple(a)


def _default_emit(kind):
    return "free" if kind == "static" else "method"


def _overload_dispatch(meta, params, kind):
    """Derive (guard tuple, nargs) for an overload from the argument types and
    arities: a type test for each param that has one, plus an n_args check
    spanning the overload's required..total argument count."""
    base = 1 if kind == "instance" else 0
    g = []
    idx = base
    required = base
    total = base
    for p in params:
        # only guard on *required* params: an optional arg may be absent, so
        # testing its slot would read past n_args.
        if not p.optional and p.conv.is_check(f"args[{idx}]"):
            g.append((idx, p.conv))
        if not p.optional:
            required += p.conv.consumes
        total += p.conv.consumes
        idx += p.conv.consumes
    nargs = meta.get("nargs")
    if not nargs:
        nargs = f"== {required}" if required == total else f">= {required}"
    return tuple(g), nargs


def _build_prop_from_property(name, prop, ns, classes):
    meta = _cppmeta(prop)
    anns = getattr(prop.fget, "__annotations__", {})
    conv = None
    if "return" in anns:
        ann = _eval_ann(anns["return"], ns)
        if ann not in (None, type(None)):
            try:
                conv, _ = _conv_for(ann, ns, classes)
            except TypeError:
                conv = None   # accessor uses get_raw / a special kind instead
    kind = meta.get("emit", "") if meta.get("emit") in ("pen", "font") else ""
    return Prop(name, conv=conv, get=meta.get("get", ""), set=meta.get("set", ""),
                get_raw=meta.get("get_raw", ""), kind=kind,
                doc=(prop.fget.__doc__ or "").strip())


def _build_make_new(func, api, ns, classes):
    """Build a generic make_new from __init__ overloads / signature.

    Buffer-protocol constructors (image) are flagged kind='image' via @cpp.
    """
    meta = _cppmeta(func)
    if meta.get("emit") == "image":
        return New(kind="image", finaliser=api["finaliser"],
                   doc=(func.__doc__ or "").strip())
    field = api["field"]
    overloads = typing.get_overloads(func) or [func]
    variants = []
    for ov in overloads:
        sig = inspect.signature(ov)
        ps = [n for n in sig.parameters if n != "self"]
        # an optional-args ctor (all defaulted) yields both the 0-arg and full form
        required = [n for n, p in sig.parameters.items()
                    if n != "self" and p.default is inspect.Parameter.empty]
        anns = getattr(ov, "__annotations__", {})
        om = _cppmeta(ov)
        if om.get("args"):
            # explicit assignment override(s): "lhs = rhs"
            pairs = []
            for a in om["args"]:
                lhs, rhs = a.split("=", 1)
                pairs.append((lhs.strip(), rhs.strip()))
            variants.append(NewVariant(len(ps), assigns=tuple(pairs)))
            continue
        # field-assignment convention: self->FIELD.<param> = get(args[i])
        full = []
        for i, n in enumerate(ps):
            conv, _ = _conv_for(_eval_ann(anns[n], ns), ns, classes)
            full.append((f"self->{field}.{n}", conv.from_mp(f"args[{i}]")))
        if not required:
            variants.append(NewVariant(0))
        variants.append(NewVariant(len(ps), assigns=tuple(full)))
    # de-dup by nargs preserving order
    seen, uniq = set(), []
    for v in variants:
        if v.nargs in seen:
            continue
        seen.add(v.nargs)
        uniq.append(v)
    return New(variants=tuple(uniq), finaliser=api["finaliser"],
               doc=(func.__doc__ or "").strip(),
               error=meta.get("error", "invalid parameters"))


def _build_binop(dunder, func, ns, classes, t):
    op, sym = _BINOPS[dunder]
    meta = _cppmeta(func)
    anns = getattr(func, "__annotations__", {})
    other = _eval_ann(anns["other"], ns)
    # split unions (vec2 | float) into individual rhs cases
    members = typing.get_args(other) if typing.get_origin(other) else (other,)
    fld = t.field
    cases = []
    default = meta.get("default", "false" if op == "EQUAL" else
                       ("true" if op == "NOT_EQUAL" else None))
    default = {"false": "mp_const_false", "true": "mp_const_true"}.get(default, "MP_OBJ_NULL")
    for m in members:
        if m is float or m is int:
            if op in ("EQUAL", "NOT_EQUAL"):
                res = f"mp_obj_new_bool(lhs->{fld} {sym} v)"
            else:
                res = t.box.format(meta.get("result") or f"lhs->{fld} {sym} v")
            cases.append((F32, res))
        elif m in classes:
            if op in ("EQUAL", "NOT_EQUAL"):
                res = f"mp_obj_new_bool(lhs->{fld} {sym} rhs->{fld})"
            else:
                res = t.box.format(meta.get("result") or f"lhs->{fld} {sym} rhs->{fld}")
            cases.append((CONVERTERS[m.__name__], res))
    return BinOp(op, cases=tuple(cases), default=default,
                 doc=(func.__doc__ or "").strip())


def load() -> list[Type]:
    """Import the api package (if needed) and build the Type IR."""
    import importlib
    importlib.import_module("api")

    classes = set(pv.REGISTRY)
    # shared namespace for evaluating string annotations
    ns = {**{c.__name__: c for c in pv.REGISTRY}, **vars(pv),
          "Self": typing.Self, "tuple": tuple, "list": list, "None": None,
          "Optional": typing.Optional}

    types: list[Type] = []
    # pass 1: create Type shells + register converters (so cross-refs resolve)
    shells = {}
    for cls in pv.REGISTRY:
        api = cls.__pv_api__
        name = cls.__name__
        obj = f"{name}_obj_t"
        ptr = api["ptr"]
        arg_read = api["arg_read"] or f"(({obj} *)MP_OBJ_TO_PTR({{0}}))->{api['field']}"
        arg_type = api["arg_type"] or (f"{api['cpp']} *" if ptr else api["cpp"])
        box = api["box"]
        if box is None and name in ("vec2", "rect", "mat3", "shape", "color", "brush"):
            box = f"pv::box_{name}({{0}})"
        t = Type(
            name=name, mp_type=f"type_{name}", obj_struct=obj, cpp_class=api["cpp"],
            field=api["field"], field_is_ptr=ptr, arg_read=arg_read,
            arg_type=arg_type, box=box, has_buffer=api["buffer"],
            del_stmt=api["del_stmt"], del_native=api["del_native"],
            includes=api["includes"],
        )
        CONVERTERS[name] = t.converter()
        shells[name] = t
        types.append(t)

    # pass 2: fill in members from each stub class
    for cls in pv.REGISTRY:
        t = shells[cls.__name__]
        api = cls.__pv_api__

        if api["print"]:
            t.has_print = True
            t.print_fmt = api["print"][0]
            t.print_args = tuple(api["print"][1:])
        for pc in api["palette"]:
            t.palette.append(PaletteColor(pc.name, pc.rgba, pc.tufty, pc.doc))
        if t.palette:
            t.has_attr = True  # colour exposes `p` + palette

        # simple field properties from class-level annotations
        cls_anns = cls.__dict__.get("__annotations__", {})
        for pname, raw in cls_anns.items():
            conv, _ = _conv_for(_eval_ann(raw, ns), ns, classes)
            aliases = tuple(k for k, v in api["aliases"].items() if v == pname)
            t.props.append(Prop(pname, conv=conv, get=f"self->{t.field}.{pname}",
                                 set=f"self->{t.field}.{pname}", aliases=aliases))
            t.has_attr = True

        for key, val in cls.__dict__.items():
            if key in ("__pv_api__", "__annotations__", "__dict__", "__weakref__",
                       "__doc__", "__module__", "__qualname__", "__firstlineno__",
                       "__static_attributes__"):
                continue
            if key == "__init__":
                t.has_make_new = True
                t.make_new = _build_make_new(val, api, ns, classes)
                continue
            if key in _BINOPS:
                t.has_binary_op = True
                t.binops.append(_build_binop(key, val, ns, classes, t))
                continue
            if key == "__del__":
                t.has_del = True
                continue
            if isinstance(val, staticmethod):
                t.members.append(_build_method(key, val.__func__, "static", ns, classes, cls.__name__))
            elif isinstance(val, property):
                t.props.append(_build_prop_from_property(key, val, ns, classes))
                t.has_attr = True
            elif inspect.isfunction(val):
                t.members.append(_build_method(key, val, "instance", ns, classes, cls.__name__))
            elif isinstance(val, pv._Const):  # class constant (e.g. shape.ALIGN_OUTER)
                t.consts.append(Const(key, val.cpp, doc=val.doc))

        if api["del_stmt"] or api["del_native"]:
            t.has_del = True

    return types
