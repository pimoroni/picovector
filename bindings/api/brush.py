"""brush — advanced fill brushes (gradient, image, pattern, effects).

Assign a brush to ``image.pen`` before drawing.  All factories are static and
construct a GC-heap brush (``m_new_class``).
"""

from __future__ import annotations

from typing import Annotated

from pv import api, cpp, const, overload, Range, ColorStops, Pattern8


@api("brush_t", field="brush", ptr=True,
     arg_read="((brush_obj_t *)MP_OBJ_TO_PTR({0}))->brush", arg_type="brush_t *")
class brush:
    """Advanced fill brush factory."""

    LINEAR = const("GRADIENT_LINEAR", "Linear gradient (along the p1→p2 axis).")
    RADIAL = const("GRADIENT_RADIAL", "Radial gradient (outward from p1).")

    # pattern(c1, c2, index | tuple[8]) -------------------------------------
    @overload
    def pattern(c1: color, c2: color, index: Annotated[int, Range(0, 37)]) -> brush:
        "Pre-baked checkerboard pattern by index (0–37)."
    @overload
    def pattern(c1: color, c2: color, pattern: Pattern8) -> brush:
        "Custom pattern from an 8-element row-bitmask tuple."

    @staticmethod
    @cpp(call="pattern_brush_t", emit="mnew",
         error="invalid parameter, expected brush.pattern(color, color, index | tuple[8])")
    def pattern(c1, c2, p) -> brush:
        "Checkerboard pattern brush. Args: c1, c2 (colors), index (0–37) or 8-tuple."

    # image(img, transform=None) --------------------------------------------
    @overload
    def image(img: image) -> brush: "Image brush at the origin."
    @overload
    @cpp(args="img &transform")
    def image(img: image, transform: mat3) -> brush: "Image brush positioned by a mat3."

    @staticmethod
    @cpp(call="image_brush_t", emit="mnew",
         error="invalid parameter, expected brush.image(image, [mat3])")
    def image(img, transform=None) -> brush:
        "Image brush. Args: img (image). Optional: transform (mat3)."

    # gradient(type, x1, y1, x2, y2, stops, transform=None) -----------------
    @staticmethod
    @cpp(call="gradient_brush_t", emit="mnew",
         args="(gradient_type_t)type x1 y1 x2 y2 stops_positions stops_colors stops_n transform")
    def gradient(type: int, x1: float, y1: float, x2: float, y2: float,
                 stops: ColorStops, transform: mat3 | None = None) -> brush:
        "Gradient brush. type: brush.LINEAR or brush.RADIAL; stops: list of "
        "(position 0–1, color), up to 16."

    # single-arg effect brushes ---------------------------------------------
    @staticmethod
    @cpp(call="pixelate_brush_t", emit="mnew")
    def pixelate(size: Annotated[int, Range(1, None, clamp=True)]) -> brush:
        "Mosaic the shape's area, block size in pixels."

    @staticmethod
    @cpp(call="blur_brush_t", emit="mnew")
    def blur(radius: Annotated[int, Range(1, None, clamp=True)]) -> brush:
        "Box-blur the shape's area from the target."

    @staticmethod
    @cpp(call="brightness_brush_t", emit="mnew")
    def lighten(amount: int) -> brush:
        "Add amount (0–255) to each channel of the backdrop."

    @staticmethod
    @cpp(call="brightness_brush_t", emit="mnew", args="-amount")
    def darken(amount: int) -> brush:
        "Subtract amount (0–255) from each channel of the backdrop."
