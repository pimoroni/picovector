"""pixel_font — bitmap font loaded from a .ppf file.

As with :mod:`font`, ``load`` and ``__del__`` are file/heap procedures and live
in ``native/pixel_font.cpp``; the attributes and registration are declarative.
"""

from __future__ import annotations

from pv import api, cpp, native


@api("pixel_font_t", field="font", ptr=True, del_native=True,
     print=("pixel_font()",),
     arg_read="((pixel_font_obj_t *)MP_OBJ_TO_PTR({0}))->font", arg_type="pixel_font_t *")
class pixel_font:
    """Bitmap font loaded from a .ppf file."""

    @staticmethod
    @native
    def load(path: str) -> pixel_font:
        "Load a bitmap font from a .ppf file path. Returns a pixel_font."

    @property
    @cpp(get="self->font->height")
    def height(self) -> int:
        "Glyph cell height in pixels (read-only)."

    @property
    @cpp(get_raw="mp_obj_new_str(self->font->name, strlen(self->font->name))")
    def name(self) -> str:
        "Font name (read-only)."
