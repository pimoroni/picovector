"""rect — axis-aligned rectangle (x, y, w, h)."""

from __future__ import annotations

from typing import Self

from pv import api, cpp, same, overload, XY


@api("rect_t", field="r", arg_read="mp_obj_get_rect({0})", arg_type="rect_t",
     finaliser=True, aliases={"l": "x", "t": "y"},
     print=("rect(%f, %f -> %f x %f)", "self->r.x", "self->r.y", "self->r.w", "self->r.h"))
class rect:
    """Axis-aligned rectangle."""

    x: float    #: Left edge (alias: l).
    y: float    #: Top edge (alias: t).
    w: float    #: Width.
    h: float    #: Height.

    @overload
    def __init__(self): "rect()"
    @overload
    @cpp(args=["self->r = mp_obj_get_rect(args[0])"])
    def __init__(self, source: rect): "rect(rect) — copy."
    @overload
    def __init__(self, x: float, y: float, w: float, h: float): "rect(x, y, w, h)."

    def __init__(self, *args):   # impl placeholder; overloads above define it
        "rect(), rect(rect) or rect(x, y, w, h)."

    # computed edges --------------------------------------------------------
    @property
    @cpp(get="self->r.w + self->r.x", set="self->r.w = {0} - self->r.x")
    def r(self) -> float:
        "Right edge (x + w)."

    @property
    @cpp(get="self->r.h + self->r.y", set="self->r.h = {0} - self->r.y")
    def b(self) -> float:
        "Bottom edge (y + h)."

    # methods ---------------------------------------------------------------
    @cpp(args="a1 a2 a1 a2")
    def deflate(self, a1: float, a2: float = same("a1")) -> Self:
        "Shrink by amount on each side, in place. deflate(a) or deflate(x, y)."

    @cpp(args="a1 a2 a1 a2")
    def inflate(self, a1: float, a2: float = same("a1")) -> Self:
        "Grow by amount on each side, in place. inflate(a) or inflate(x, y)."

    def intersection(self, other: rect) -> rect:
        "Return the intersection with another rect (empty if disjoint)."

    def intersects(self, other: rect) -> bool:
        "True if this rect overlaps with other."

    def empty(self) -> bool:
        "True if this rect has zero area."

    @cpp(args="p.x p.y")
    def offset(self, p: XY) -> Self:
        "Shift by (x, y), in place. Also accepts a single vec2."

    @overload
    def contains(self, other: rect) -> bool: "True if this rect fully contains another rect."
    @overload
    def contains(self, point: vec2) -> bool: "True if this rect contains a point."

    @cpp(error="invalid parameters, expected either rect(x, y, w, h) or vec2(x, y)")
    def contains(self, other) -> bool:
        "True if this rect fully contains another rect or a point (vec2)."
