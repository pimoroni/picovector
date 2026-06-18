"""color — an RGBA / HSV / OKLCH colour with a built-in palette."""

from __future__ import annotations

from pv import api, cpp, Palette


# Dawnbringer-16 based palette (+ Badger e-ink greys). black/white differ on
# Tufty. Order matches the original color.cpp locals dict.
PALETTE = [
    Palette("black", (0x00, 0x00, 0x00, 0xff), tufty=(0x14, 0x1e, 0x28, 0xff), doc="Black."),
    Palette("grape", (0x44, 0x24, 0x34, 0xff), doc="Grape / purple."),
    Palette("navy", (0x30, 0x34, 0x6d, 0xff), doc="Navy blue."),
    Palette("grey", (0x4e, 0x4a, 0x4e, 0xff), doc="Grey."),
    Palette("brown", (0x85, 0x4c, 0x30, 0xff), doc="Brown."),
    Palette("green", (0x34, 0x65, 0x24, 0xff), doc="Green."),
    Palette("red", (0xd0, 0x46, 0x48, 0xff), doc="Red."),
    Palette("taupe", (0x75, 0x71, 0x61, 0xff), doc="Taupe."),
    Palette("blue", (0x59, 0x7d, 0xce, 0xff), doc="Blue."),
    Palette("orange", (0xd2, 0x7d, 0x2c, 0xff), doc="Orange."),
    Palette("smoke", (0x85, 0x95, 0xa1, 0xff), doc="Smoke (pale grey-blue)."),
    Palette("lime", (0x6d, 0xaa, 0x2c, 0xff), doc="Lime green."),
    Palette("latte", (0xd2, 0xaa, 0x99, 0xff), doc="Latte (warm beige)."),
    Palette("cyan", (0x6d, 0xc2, 0xca, 0xff), doc="Cyan."),
    Palette("yellow", (0xda, 0xd4, 0x5e, 0xff), doc="Yellow."),
    Palette("white", (0xff, 0xff, 0xff, 0xff), tufty=(0xde, 0xee, 0xd6, 0xff), doc="White."),
    Palette("transparent", (0x00, 0x00, 0x00, 0x00), doc="Fully transparent (alpha 0)."),
    Palette("light_grey", (0xc0, 0xc0, 0xc0, 0xff), doc="Light grey (Badger e-ink)."),
    Palette("dark_grey", (0x40, 0x40, 0x40, 0xff), doc="Dark grey (Badger e-ink)."),
]


@api("color_t", field="c", ptr=True, box="pv::box_color({0})",
     arg_read="(*((color_obj_t *)MP_OBJ_TO_PTR({0}))->c)", arg_type="color_t",
     includes=("blend.hpp",), palette=PALETTE)
class color:
    """An RGBA / HSV / OKLCH colour with a built-in palette."""

    @staticmethod
    @cpp(call="rgb_color_t", emit="new")
    def rgb(r: int, g: int, b: int, a: int = 255) -> color:
        "Create a colour from RGB values (0–255 each). Optional alpha (0–255)."

    @staticmethod
    @cpp(call="hsv_color_t", emit="new", args="(int)fmod((float)h,360.0f) s v a")
    def hsv(h: int, s: int, v: int, a: int = 255) -> color:
        "Create a colour from HSV components."

    @staticmethod
    @cpp(call="oklch_color_t", emit="new")
    def oklch(l: int, c: int, h: int, a: int = 255) -> color:
        "Create a colour from OKLCH components."

    @property
    @cpp(get="self->c->_p")
    def p(self) -> int:
        "Premultiplied packed RGBA word (read-only)."
