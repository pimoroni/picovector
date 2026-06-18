"""shape — a vector shape (one or more contours) plus a transform."""

from __future__ import annotations

from typing import Self, Annotated

from pv import api, cpp, const, same, Range, XY, XYWH, PathList


@api("shape_t", field="shape", ptr=True, finaliser=True, box="pv::box_shape({0})",
     del_stmt="m_del_class(shape_t, self->shape)",
     arg_read="((shape_obj_t *)MP_OBJ_TO_PTR({0}))->shape", arg_type="shape_t *")
class shape:
    """A filled/stroked vector shape. Build with the static factories; assign a
    transform; draw with image.shape()."""

    # stroke() flags — OR one value per group; every default is the 0 value
    ALIGN_OUTER = const("ALIGN_OUTER", "Stroke alignment: outside the path.")
    ALIGN_INNER = const("ALIGN_INNER", "Stroke alignment: inside the path.")
    ALIGN_CENTER = const("ALIGN_CENTER", "Stroke alignment: centred on the path.")
    PATH_CLOSED = const("PATH_CLOSED", "Path is closed (endpoint joins the start).")
    PATH_OPEN = const("PATH_OPEN", "Path is open (adds end caps).")
    JOIN_MITER = const("JOIN_MITER", "Stroke join: mitre (sharp corner).")
    JOIN_ROUND = const("JOIN_ROUND", "Stroke join: rounded corner.")
    JOIN_BEVEL = const("JOIN_BEVEL", "Stroke join: bevelled (flat cut).")
    CAP_BUTT = const("CAP_BUTT", "Stroke cap: flat, ends at the endpoint.")
    CAP_ROUND = const("CAP_ROUND", "Stroke cap: rounded semicircle.")
    CAP_SQUARE = const("CAP_SQUARE", "Stroke cap: square, extends past the endpoint.")

    # ── factories ──────────────────────────────────────────────────────────
    @staticmethod
    @cpp(emit="expr", args="paths_shape")
    def custom(paths: PathList) -> shape:
        "Custom shape from one or more lists of vec2 points (extra lists = holes)."

    @staticmethod
    @cpp(args="c.x c.y sides r")
    def regular_polygon(c: XY, r: float, sides: int) -> shape:
        "Regular polygon. Args: centre, radius, number of sides."

    @staticmethod
    @cpp(args="c.x c.y r")
    def circle(c: XY, r: float) -> shape:
        "Circle. Args: centre (vec2 or x, y), radius."

    @staticmethod
    @cpp(args="c.x c.y rx ry")
    def ellipse(c: XY, rx: float, ry: float) -> shape:
        "Ellipse. Args: centre, x-radius, y-radius."

    @staticmethod
    @cpp(args="r.x r.y r.w r.h")
    def rectangle(r: XYWH) -> shape:
        "Rectangle. Args: a rect, or x, y, w, h."

    @staticmethod
    @cpp(args="r.x r.y r.w r.h r1 r2 r3 r4")
    def rounded_rectangle(r: XYWH, r1: float, r2: float = same("r1"),
                          r3: float = same("r1"), r4: float = same("r1")) -> shape:
        "Rounded rectangle. Corner radii (TL, TR, BR, BL) default to r1."

    @staticmethod
    @cpp(args="c.x c.y s n")
    def squircle(c: XY, s: float,
                 n: Annotated[float, Range(2, None, clamp=True)] = 4.0) -> shape:
        "Squircle (super-ellipse). Args: centre, size; n is the exponent (default 4)."

    @staticmethod
    @cpp(args="c.x c.y from_a to_a inner outer")
    def arc(c: XY, inner: float, outer: float, from_a: float, to_a: float) -> shape:
        "Annular sector / arc. Args: centre, inner & outer radius, from/to angle (degrees)."

    @staticmethod
    @cpp(args="c.x c.y from_a to_a r")
    def pie(c: XY, r: float, from_a: float, to_a: float) -> shape:
        "Pie / sector slice. Args: centre, radius, from/to angle (degrees)."

    @staticmethod
    @cpp(args="c.x c.y points outer inner")
    def star(c: XY, points: int, outer: float, inner: float) -> shape:
        "Star polygon. Args: centre, point count, outer & inner radius."

    @staticmethod
    @cpp(args="p1.x p1.y p2.x p2.y w")
    def line(p1: XY, p2: XY, w: float) -> shape:
        "Stroked line shape of width w between two points."

    # ── instance methods ───────────────────────────────────────────────────
    def stroke(self, width: float, flags: int = 0, miter_limit: float = 4.0) -> Self:
        "Replace this shape with its stroked outline. flags: OR of "
        "ALIGN_*/PATH_*/JOIN_*/CAP_* (each default is the 0 value). Returns self."

    def bounds(self) -> rect:
        "Device-space bounding box (local bbox run through the current transform)."

    @property
    @cpp(get="self->shape->transform", set="self->shape->transform = {0}")
    def transform(self) -> mat3:
        "The shape's 3×3 transform (applied when drawn / when measuring bounds)."
