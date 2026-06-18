"""algorithm — geometry helpers (line clipping, DDA grid marching, raycasting).

All three build/observe Python containers or mutate their vec2 arguments in
place, so their bodies live in ``native/algorithm.cpp``; the DSL owns their
registration and documented surface.
"""

from __future__ import annotations

from pv import api, native, Buffer


@api(field="")
class algorithm:
    """Geometry algorithms."""

    @staticmethod
    @native
    def clip_line(p1: vec2, p2: vec2, bounds: rect) -> bool:
        "Clip a line segment to a rect, updating p1/p2 in place. False if outside."

    @staticmethod
    @native
    def dda(origin: vec2, angle: float, depth: float) -> list:
        "Ray-march a tile grid (DDA). Returns a list of (point, cell, edge, "
        "offset, distance) hit tuples up to depth."

    @staticmethod
    @native
    def raycast(origin: vec2, angle: float, fov: float, rays: int, max_dist: float,
                map: Buffer, width: int, height: int, screen_width: int) -> tuple:
        "2D raycaster over a byte tilemap. Returns one hit-list per ray."
