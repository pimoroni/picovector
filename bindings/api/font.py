"""font — vector font loaded from a .af file.

``load`` parses the binary .af container and ``__del__`` frees the backing
buffer; both are inherently procedural file/heap operations, so their bodies
live in ``native/font.cpp``.  The DSL still owns the type's registration,
surface and docstrings.
"""

from __future__ import annotations

from pv import api, native


@api("font_t", field="font", del_native=True, print=("font()",),
     arg_read="&((font_obj_t *)MP_OBJ_TO_PTR({0}))->font", arg_type="font_t *")
class font:
    """Vector font loaded from a .af file."""

    @staticmethod
    @native
    def load(path: str) -> font:
        "Load a vector font from a .af file path. Returns a font."
