"""vec2 — 2D vector / point."""

from __future__ import annotations

from pv import api, cpp


@api("vec2_t", field="v", arg_read="mp_obj_get_vec2({0})", arg_type="vec2_t",
     print=("vec(%f, %f)", "self->v.x", "self->v.y"))
class vec2:
    """2D vector / point."""

    x: float    #: X component.
    y: float    #: Y component.

    def __init__(self, x: float = 0, y: float = 0):
        "Create a vector: vec2() (zero) or vec2(x, y)."

    # scalar results --------------------------------------------------------
    def length(self) -> float:
        "Magnitude (Euclidean length) of this vector."

    def length_squared(self) -> float:
        "Squared magnitude (faster than length() — no square root)."

    def dot(self, other: vec2) -> float:
        "Dot product with another vec2."

    def cross(self, other: vec2) -> float:
        "Cross product (scalar z-component) with another vec2."

    def distance(self, other: vec2) -> float:
        "Euclidean distance to another vec2."

    def distance_squared(self, other: vec2) -> float:
        "Squared distance to another vec2 (faster than distance())."

    def angle(self) -> float:
        "Angle of this vector in radians (atan2(y, x))."

    def angle_to(self, other: vec2) -> float:
        "Angle from this vector to another, in radians."

    # vector results (a new vec2; the original is unchanged) ----------------
    def normalized(self) -> vec2:
        "Return a unit vector in the same direction."

    def perpendicular(self) -> vec2:
        "Return a vector perpendicular to this one (rotated 90°)."

    def abs(self) -> vec2:
        "Return component-wise absolute value."

    def rotated(self, angle: float) -> vec2:
        "Return this vector rotated by angle (radians)."

    def lerp(self, other: vec2, t: float) -> vec2:
        "Linear interpolation toward other. t=0 returns self, t=1 returns other."

    def reflect(self, normal: vec2) -> vec2:
        "Reflect this vector around the given normal vec2."

    def clamp_length(self, max_length: float) -> vec2:
        "Return this vector clamped to at most max_length magnitude."

    @cpp(emit="mutate")
    def transform(self, m: mat3) -> None:
        "Apply a mat3 transformation to this vector, in place."

    # operators -------------------------------------------------------------
    def __add__(self, other: vec2) -> vec2: "Component-wise addition."
    def __sub__(self, other: vec2) -> vec2: "Component-wise subtraction."
    def __mul__(self, other: vec2 | float) -> vec2: "Multiply by a vec2 or scalar."
    def __truediv__(self, other: vec2 | float) -> vec2: "Divide by a vec2 or scalar."
    def __eq__(self, other: vec2) -> bool: "Equality."
    def __ne__(self, other: vec2) -> bool: "Inequality."
