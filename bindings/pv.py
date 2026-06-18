"""pv — the authoring surface for the PicoVector binding stubs.

The ``api/*.py`` files describe the PicoVector MicroPython surface as ordinary,
type-annotated Python stub classes — they read like a ``.pyi`` of the library
as if it were written in Python.  This module provides the small amount of
vocabulary those stubs need:

* :class:`Range` — value-range metadata, attached via ``Annotated[int, Range(...)]``.
* pseudo-types (:data:`XY`, :data:`XYWH`, :data:`ColorStops`, …) for arguments
  that don't map onto a single Python type (e.g. "a vec2 *or* two floats").
* :func:`api` — class decorator carrying the C++ wiring for a whole type.
* :func:`cpp` — method/property decorator used **only** where the convention
  (Python name == C++ callee, Python arg order == C++ arg order) breaks.
* :func:`native` — marks the handful of irreducibly-procedural members whose
  body is hand-written in ``native/*.cpp``.
* ``overload`` — re-exported from :mod:`typing` for genuinely overloaded members.

Nothing here emits code; the decorators only stash metadata which ``model.py``
introspects (alongside the real signatures and docstrings) to build the binding
IR consumed by ``generate.py``.
"""

from dataclasses import dataclass
from typing import Annotated, overload, Optional  # noqa: F401  (overload re-exported)

__all__ = [
    "Range", "api", "cpp", "native", "overload", "Annotated", "same", "Palette",
    "const",
    "XY", "XYWH", "ColorStops", "Pattern8", "PathList", "ShapeOrList",
    "Filter", "Buffer", "NEAREST", "BILINEAR", "BICUBIC",
]

# texture-filter constants usable as default argument values (their C++ enum
# names share these spellings, so the generated code reads the same).
NEAREST = 0
BILINEAR = 1
BICUBIC = 2


@dataclass(frozen=True)
class _Same:
    """Default sentinel: an optional argument defaults to another argument's
    value (e.g. ``mat3.scale(x, y=same('x'))``)."""
    name: str


def same(name: str) -> _Same:
    return _Same(name)


@dataclass(frozen=True)
class _Const:
    cpp: str
    doc: str = ""


def const(cpp: str, doc: str = "") -> _Const:
    """A class-level enum/flag constant. ``cpp`` is the C++ rvalue placed in the
    type's locals dict (e.g. ``"ALIGN_OUTER"``); the attribute name is the
    Python-facing name."""
    return _Const(cpp, doc)


@dataclass(frozen=True)
class Palette:
    """A predefined ``color`` instance. ``tufty`` overrides the RGBA on the
    Tufty display (matching the original ``#ifdef TUFTY`` tweak for black/white)."""
    name: str
    rgba: tuple
    tufty: "Optional[tuple]" = None
    doc: str = ""


# ─────────────────────────────────────────────────────────────────────────────
@dataclass(frozen=True)
class Range:
    """Acceptable range for a numeric argument.

    ``clamp=True`` silently clamps out-of-range values (e.g. ``brush.blur`` /
    ``pixelate`` floor at 1); ``clamp=False`` raises ``ValueError``.  Either
    bound may be ``None`` for one-sided ranges.
    """
    lo: Optional[float] = None
    hi: Optional[float] = None
    clamp: bool = False


# ── pseudo-types ─────────────────────────────────────────────────────────────
class _Pseudo:
    """An opaque annotation marker for an argument shape with no single Python
    type.  Used purely for readability + dispatch; resolved by the loader."""

    def __init__(self, name: str, doc: str):
        self.name = name
        self.__doc__ = doc

    def __repr__(self):
        return self.name


XY = _Pseudo("XY", "A vec2, or two floats (x, y).")
XYWH = _Pseudo("XYWH", "A rect, or four floats (x, y, w, h).")
ColorStops = _Pseudo("ColorStops", "A sequence of (position, color) gradient stops.")
Pattern8 = _Pseudo("Pattern8", "An 8-element tuple of row bitmasks (custom pattern).")
PathList = _Pseudo("PathList", "One or more lists of vec2 points (shape contours).")
ShapeOrList = _Pseudo("ShapeOrList", "A shape, or a list of shapes.")
Filter = _Pseudo("Filter", "A texture filter constant: NEAREST, BILINEAR or BICUBIC.")
Buffer = _Pseudo("Buffer", "A writable buffer-protocol object (bytes/bytearray/array).")


# ── decorators ───────────────────────────────────────────────────────────────
#: stub classes in declaration order (also = module-globals table order)
REGISTRY: list = []


def api(cpp=None, *, field="", ptr=False, finaliser=False, buffer=False,
        del_native=False, del_stmt="", includes=(), print=None,
        aliases=None, palette=(), arg_read=None, arg_type=None, box=None):
    """Class decorator declaring the C++ wiring for a whole picovector type.

    ``cpp`` is the underlying C++ class (e.g. ``"vec2_t"``); ``field`` is the
    obj-struct member holding it (``self->FIELD``); ``ptr`` says whether that
    member is a pointer.  The remaining flags toggle the type's MicroPython
    slots (finaliser, buffer protocol, custom ``__del__``, ``__repr__`` format)
    and supply rarely-needed extras (extra includes, attribute aliases, a colour
    palette).
    """
    def deco(cls):
        cls.__pv_api__ = dict(
            cpp=cpp, field=field, ptr=ptr, finaliser=finaliser, buffer=buffer,
            del_native=del_native, del_stmt=del_stmt, includes=tuple(includes),
            print=print, aliases=dict(aliases or {}), palette=tuple(palette),
            arg_read=arg_read, arg_type=arg_type, box=box,
        )
        REGISTRY.append(cls)
        return cls
    return deco


def _unwrap(fn):
    """Reach the underlying function of a staticmethod/classmethod/property."""
    if isinstance(fn, (staticmethod, classmethod)):
        return fn.__func__
    if isinstance(fn, property):
        return fn.fget
    return fn


def cpp(call=None, *, args=None, emit=None, native=False, get=None, set=None,
        get_raw=None, error=None, result=None, default=None, recv=None, box=None):
    """Method/property decorator for the cases convention can't express.

    * ``call`` — C++ callee name when it differs from the Python method name
      (e.g. ``rgb`` → ``rgb_color_t``).
    * ``args`` — explicit C++ call argument list (space-separated names /
      expressions) when the order differs from the Python signature, e.g.
      ``arc``'s ``"c.x c.y from_a to_a inner outer"``.
    * ``emit`` — how the callee is invoked: ``"free"`` (free function),
      ``"new"`` (``new T(...)``), ``"mnew"`` (GC ``m_new_class``), ``"expr"``
      (the single arg is a bare expression), default ``"method"``.
    * ``native`` — body supplied by ``native/*.cpp``.
    * ``get`` / ``set`` / ``get_raw`` — property accessor C++ (see model.py).
    * ``result`` / ``default`` — binary-operator result / fall-through.
    * ``error`` — message raised when no overload matches.
    """
    info = dict(call=call, args=args, emit=emit, native=native, get=get,
                set=set, get_raw=get_raw, error=error, result=result,
                default=default, recv=recv, box=box)
    info = {k: v for k, v in info.items() if v not in (None, False)}

    def deco(fn):
        target = _unwrap(fn)
        merged = {**getattr(target, "__pv_cpp__", {}), **info}
        target.__pv_cpp__ = merged
        return fn
    return deco


def native(fn):
    """Shorthand for ``@cpp(native=True)`` — body lives in ``native/*.cpp``."""
    target = _unwrap(fn)
    target.__pv_cpp__ = {**getattr(target, "__pv_cpp__", {}), "native": True}
    return fn
