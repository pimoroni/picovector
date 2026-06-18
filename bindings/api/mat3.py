"""mat3 — 3×3 transformation matrix for 2D transforms."""

from __future__ import annotations

from pv import api, cpp, same, XY


@api("mat3_t", field="m", arg_read="((mat3_obj_t *)MP_OBJ_TO_PTR({0}))->m",
     arg_type="mat3_t")
class mat3:
    """3×3 transformation matrix for 2D transforms."""

    @cpp(args=["self->m = mat3_t()"])
    def __init__(self):
        "Create an identity matrix."

    def rotate(self, degrees: float) -> mat3:
        "Rotate by degrees. Returns a new mat3."

    def rotate_radians(self, radians: float) -> mat3:
        "Rotate by radians. Returns a new mat3."

    @cpp(args="p.x p.y")
    def translate(self, p: XY) -> mat3:
        "Translate by (x, y). Also accepts a single vec2. Returns a new mat3."

    def scale(self, x: float, y: float = same("x")) -> mat3:
        "Scale by (x, y). Pass one value to scale uniformly. Returns a new mat3."

    def multiply(self, other: mat3) -> mat3:
        "Multiply this matrix by another mat3. Returns a new mat3."

    def inverse(self) -> mat3:
        "Return the inverse of this matrix."

    @cpp(result="lhs->m.multiply(rhs->m)")
    def __mul__(self, other: mat3) -> mat3:
        "Matrix multiplication."
