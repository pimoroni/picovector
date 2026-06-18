"""image — an RGBA pixel buffer: the screen, off-screen canvases and sub-views.

The bulk of the drawing surface is generated; the members that build nested
Python results, dispatch by name, decode files or track a parent view
(``load``/``load_into``/``window``/``text``/``measure_text``/``shapes``/``batch``)
are inherently procedural and live in ``native/image.cpp`` — the DSL still owns
their signatures, docstrings and registration.
"""

from __future__ import annotations

from pv import (api, cpp, native, overload, const,
                XY, XYWH, Filter, Buffer, ShapeOrList, NEAREST)


@api("image_t", field="image", ptr=True, finaliser=True, buffer=True,
     del_stmt="if(self->image) { m_del_class(image_t, self->image); }",
     print=("image(%d x %d)", "int(self->image->bounds().w)", "int(self->image->bounds().h)"),
     arg_read="((image_obj_t *)MP_OBJ_TO_PTR({0}))->image", arg_type="image_t *")
class image:
    """An RGBA pixel buffer you can draw into and blit around."""

    # antialias / fill-rule / texture-filter constants
    X4 = const("antialias_t::X4", "Anti-aliasing: 4× supersample.")
    X2 = const("antialias_t::X2", "Anti-aliasing: 2× supersample.")
    OFF = const("antialias_t::OFF", "Anti-aliasing disabled.")
    EVEN_ODD = const("fill_rule_t::EVEN_ODD", "Fill rule: even-odd winding.")
    NON_ZERO = const("fill_rule_t::NON_ZERO", "Fill rule: non-zero winding.")
    NEAREST = const("filter_t::NEAREST", "Texture filter: nearest-neighbour (fastest).")
    BILINEAR = const("filter_t::BILINEAR", "Texture filter: bilinear (smooth).")
    BICUBIC = const("filter_t::BICUBIC", "Texture filter: bicubic (highest quality).")

    @cpp(emit="image")
    def __init__(self, width: int, height: int, buffer: Buffer = None):
        "image(width, height) allocates a buffer; pass an existing buffer to wrap it."

    # ── properties ──────────────────────────────────────────────────────────
    @property
    @cpp(get="self->image->bounds().w")
    def width(self) -> int: "Width in pixels (read-only)."

    @property
    @cpp(get="self->image->bounds().h")
    def height(self) -> int: "Height in pixels (read-only)."

    @property
    @cpp(get_raw="mp_obj_new_bytearray_by_ref(self->image->buffer_size(), self->image->ptr(0, 0))")
    def raw(self) -> None: "The framebuffer as a bytearray view (read-only, by reference)."

    @property
    @cpp(get="self->image->clip()", set="self->image->clip({0})")
    def clip(self) -> rect: "Clip rect; assign to restrict drawing."

    @property
    @cpp(get="self->image->has_palette()")
    def has_palette(self) -> bool: "True if this image is palettised (read-only)."

    @property
    @cpp(get="self->image->antialias()", set="self->image->antialias((antialias_t){0})")
    def antialias(self) -> int: "Anti-aliasing mode: OFF, X2 or X4."

    @property
    @cpp(get="self->image->fill_rule()", set="self->image->fill_rule((fill_rule_t){0})")
    def fill_rule(self) -> int: "Winding fill rule: EVEN_ODD or NON_ZERO."

    @property
    @cpp(get="self->image->alpha()", set="self->image->alpha({0})")
    def alpha(self) -> int: "Global layer alpha 0–255."

    @property
    @cpp(emit="pen")
    def pen(self) -> brush | None: "Current fill brush/colour (assign a brush or color)."

    @property
    @cpp(emit="font")
    def font(self) -> font | None: "Current font (assign a font or pixel_font)."

    # ── construction / IO (procedural → native) ─────────────────────────────
    @staticmethod
    @native
    def load(path_or_bytes, width: int = 0, height: int = 0) -> image:
        "Load a PNG/JPEG from a path or buffer. Returns an image."

    @native
    def load_into(self, path_or_bytes) -> None:
        "Load a PNG/JPEG into this buffer in place."

    @native
    def window(self, area: XYWH) -> image:
        "Return a sub-image view of this canvas (shares pixels)."

    # ── raster primitives ───────────────────────────────────────────────────
    def clear(self) -> None: "Fill the whole image with the current pen."

    @cpp(args="r")
    def rectangle(self, r: XYWH) -> None: "Filled rectangle (rect, or x, y, w, h)."

    @cpp(args="a b")
    def line(self, a: XY, b: XY) -> None: "Line between two points."

    @cpp(args="c (int)r")
    def circle(self, c: XY, r: float) -> None: "Filled circle at c with radius r."

    @cpp(args="a b c")
    def triangle(self, a: XY, b: XY, c: XY) -> None: "Filled triangle."

    def blur(self, radius: float) -> None: "Box-blur the whole image in place."
    def dither(self) -> None: "Dither the whole image in place."
    def onebit(self) -> None: "Convert to 1-bit black/white in place."
    def monochrome(self) -> None: "Convert to monochrome in place."

    @cpp(args="p.x p.y", box="pv::box_color_packed({0})")
    def get(self, p: XY) -> color: "Read the pixel colour at p."

    @cpp(args="p.x p.y")
    def put(self, p: XY) -> None: "Set the pixel at p to the current pen."

    def shape(self, s: ShapeOrList) -> None: "Draw a shape, or a list of shapes."

    @native
    def shapes(self, entries) -> None:
        "Batched shape draw: each entry is a shape or (shape, brush/color)."

    # ── text (font branching → native) ──────────────────────────────────────
    @native
    def text(self, text: str, at: XY, size: float = 12) -> None:
        "Draw text at a point using the current font."

    @native
    def measure_text(self, text: str, size: float = 0) -> tuple[float, float]:
        "Measure text in the current font. Returns (width, height). "
        "size is required for vector fonts, ignored for pixel fonts."

    # ── blitting ─────────────────────────────────────────────────────────────
    @overload
    @cpp(args="self->image at")
    def blit(self, src: image, at: vec2) -> None: "Blit src at a point."
    @overload
    @cpp(args="self->image vec2_t(x,y)")
    def blit(self, src: image, x: float, y: float) -> None: "Blit src at (x, y)."
    @overload
    @cpp(args="self->image source dst filter")
    def blit(self, src: image, source: rect, dst: rect, filter: Filter = NEAREST) -> None:
        "Blit a source rect of src into a destination rect."
    @overload
    @cpp(args="self->image dst filter")
    def blit(self, src: image, dst: rect, filter: Filter = NEAREST) -> None:
        "Blit all of src into a destination rect."

    @cpp(recv="src", error="invalid parameter, expected blit(image, point), "
         "blit(image, rect) or blit(image, source_rect, dest_rect)")
    def blit(self, src, *args) -> None:
        "Blit another image onto this one (point, rect, or src-rect → dst-rect)."

    @cpp(recv="src", args="self->image p c uv0 uv1 filter")
    def blit_vspan(self, src: image, p: XY, c: int, uv0: XY, uv1: XY,
                   filter: Filter = NEAREST) -> None:
        "Blit a vertical texture span (advanced; used by raycasters)."

    @cpp(recv="src", args="self->image p c uv0 uv1 filter")
    def blit_hspan(self, src: image, p: XY, c: int, uv0: XY, uv1: XY,
                   filter: Filter = NEAREST) -> None:
        "Blit a horizontal texture span (advanced)."

    @native
    def batch(self, commands) -> None:
        "Run a list of (method, *args) draw commands in one C loop."
