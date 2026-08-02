#pragma once

#include <cstdint>

#include "picovector.hpp"

namespace picovector {

  // A premultiplied packed RGBA word: the framebuffer's pixel format and what
  // every blend consumes. A typedef, not a type, so it changes no signature - it
  // names the role at the boundaries where "a colour" and "a pixel" are easy to
  // confuse. Channel order matches blend.hpp's _r/_g/_b/_a extractors.
  typedef uint32_t pixel_t;

  // How a colour was authored. Its components are kept in this space and read
  // back unchanged; the resolved sRGB is derived from them.
  enum color_space_t : uint8_t {
    COLOR_RGB   = 0,
    COLOR_HSV   = 1,
    COLOR_OKLCH = 2
  };

  // A colour from the user's point of view: the three components it was authored
  // with, the space they belong to, and the resolved sRGB with its premultiplied
  // word cached alongside. Components are bytes in every space, which is what
  // lets the arithmetic below behave the same way in all of them. See
  // rgb_color_t/hsv_color_t/oklch_color_t for the per-space ranges.
  //
  // Not polymorphic. The subclasses below only construct, and boxing a colour
  // slices to this base, so everything a caller can use has to live here.
  class color_t {
  public:
    uint8_t       _c0 = 0, _c1 = 0, _c2 = 0;  // components exactly as authored
    uint8_t       _a = 0;                     // alpha; exact in every space
    color_space_t _space = COLOR_RGB;
    uint8_t       _sr = 0, _sg = 0, _sb = 0;  // resolved sRGB
    pixel_t       _p = 0;                     // premultiply of the above; what blends read

    color_space_t space() const { return _space; }
    uint8_t component(int i) const { return i == 0 ? _c0 : (i == 1 ? _c1 : _c2); }

    // Resolved sRGB. Valid whatever the authoring space.
    uint8_t r() const { return _sr; }
    uint8_t g() const { return _sg; }
    uint8_t b() const { return _sb; }
    uint8_t a() const { return _a; }

    // Authored components, exactly as supplied. Each is meaningful only in its
    // own space - `l()` on an RGB colour reads a red channel - so a caller that
    // did not author the colour checks space() first. The bindings raise.
    uint8_t h() const { return _space == COLOR_OKLCH ? _c2 : _c0; }
    uint8_t s() const { return _c1; }
    uint8_t v() const { return _c2; }
    uint8_t l() const { return _c0; }
    uint8_t c() const { return _c1; }

    // ── arithmetic ────────────────────────────────────────────────────────────
    // All return a new colour in the receiver's authoring space, clamped to
    // 0..255. Nothing here mutates: a boxed colour is shared (the palette lives
    // in read-only storage) and the bindings expose no setters.
    //
    // lighten/darken/scale act on whichever component means lightness - v in
    // HSV, l in OKLCH. RGB has none, so they act on all three channels, a signed
    // per-channel add matching what brightness_brush_t does to a pixel.
    // Amounts are ints and clamp, so a caller (the bindings especially) cannot
    // wrap a component by handing over something out of range.
    color_t lighten(int amount) const;
    color_t darken(int amount) const { return lighten(-amount); }
    color_t scale(int percent) const;
    color_t with_alpha(int a) const;

    // Replace one authored component (index 0..2), in the authoring space.
    color_t with_component(int index, int value) const;

    // Blend toward `other`; t 0 is this colour, 255 is `other`. Interpolates the
    // authored components when both sides share a space, a hue by the shortest
    // way round the wheel. Across spaces it interpolates resolved sRGB and
    // returns an RGB colour, since there is no shared set of components.
    color_t mix(const color_t &other, int t) const;

    // This colour composited over `background`, weighted by its own alpha: the
    // colour it will actually land as. The same "over" the renderer performs, so
    // a caller can work out a foreground without guessing at it. The result is
    // an RGB colour, because a composited pixel knows nothing else.
    color_t over(const color_t &background) const;

    // Compares the resolved pixel, not how the colour was authored: two colours
    // that render identically are equal whichever space they came from.
    bool operator==(const color_t &o) const { return _p == o._p; }
    bool operator!=(const color_t &o) const { return _p != o._p; }

  protected:
    // Record the authored components, resolve them to sRGB, cache the premultiply.
    // The one path every colour is built through, including the subclasses.
    void author(color_space_t space, uint8_t c0, uint8_t c1, uint8_t c2, uint8_t a);
  };

  // The boxed colour has to fit one 32-byte GC block alongside its mp_obj_base_t,
  // or every colour costs two blocks and a pointer chase.
  static_assert(sizeof(color_t) == 12, "color_t has grown; check the GC block budget");

  // The subclasses below exist only to construct. They add no state: a colour
  // records which of them made it in _space, so slicing to color_t is lossless.

  // r, g, b: 0-255.
  class rgb_color_t : public color_t {
  public:
    rgb_color_t(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
  };

  // h, s, v: 0-255 each; hue is 256 counts to a full turn, and wraps.
  class hsv_color_t : public color_t {
  public:
    hsv_color_t(uint8_t h, uint8_t s, uint8_t v, uint8_t a);
  };

  // Recover straight components from a premultiplied word - what a framebuffer
  // holds. Exact at full alpha, which is the case that matters since a screen is
  // opaque; below that only what the premultiplied byte retained can come back
  // (at alpha 10 a channel was stored in 7 of 255 levels). Alpha 0 carries no
  // colour at all - every colour premultiplies to zero - so it reads back as
  // transparent black rather than pretending otherwise.
  rgb_color_t color_from_premul(pixel_t premul);

  // l, c, h: 0-255 each. Not CSS OKLCH units - l covers 0-1 lightness, c covers
  // 0-0.35 chroma, and h is 256 counts to a full turn (so 250 is 352 degrees).
  // Bytes throughout, so a lighten() step means the same thing in every space.
  class oklch_color_t : public color_t {
  public:
    oklch_color_t(uint8_t l, uint8_t c, uint8_t h, uint8_t a);
  };

}
