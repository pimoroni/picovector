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

  // Classical colour-wheel schemes, as offsets around the hue. A full turn is
  // 256 counts, which divides exactly by two and four and by three to within
  // half a degree, so these land where they should despite being bytes.
  enum color_scheme_t : uint8_t {
    SCHEME_COMPLEMENT = 0,  // 2 colours: opposite
    SCHEME_SPLIT      = 1,  // 3: either side of the opposite
    SCHEME_TRIAD      = 2,  // 3: evenly spaced thirds
    SCHEME_TETRAD     = 3,  // 4: two complementary pairs, unevenly spaced
    SCHEME_SQUARE     = 4,  // 4: evenly spaced quarters
    SCHEME_ANALOGOUS  = 5   // 3: neighbours either side
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

    // ── changing space ────────────────────────────────────────────────────────
    // The same colour authored in another space, so its components can be read
    // and its arithmetic acts on the axis you meant. Returns *this when it is
    // already in that space, which is the only exact case: anything else goes
    // through the resolved sRGB and lands on the nearest byte in each axis.
    //
    // to_oklch() is what makes an OKLCH colour readable rather than only
    // writable - a palette entry, a pixel read back, a gradient stop someone
    // authored as channels can all report a lightness, a chroma and a hue. At
    // very low chroma the hue is whatever the rounding left behind, because a
    // near-grey has no meaningful one.
    color_t to_oklch() const;
    color_t to_rgb() const;

    // ── measurement ───────────────────────────────────────────────────────────
    // All three read the resolved sRGB and ignore alpha: contrast against a
    // colour you can see through is not a question with an answer, so composite
    // it with over() first and ask about the result.

    // WCAG relative luminance, 0-1. The light the screen puts out, which is not
    // lightness - a saturated yellow and a saturated blue at the same OKLCH l
    // are nowhere near the same luminance.
    float luminance() const;

    // WCAG 2.1 contrast ratio against another colour, 1 (identical) to 21 (black
    // against white). The thresholds that get audited are 3 for large text and
    // interface components, 4.5 for body text at AA, and 7 at AAA. It is a
    // crude perceptual model, notably at the dark end, and APCA is its intended
    // replacement - but this is the number people are held to today.
    float contrast(const color_t &other) const;

    // Perceptual distance, as the straight-line distance in OKLab scaled so that
    // black to white is 100. That puts it on the scale CIEDE2000 readers expect:
    // about 2 is where a difference becomes noticeable, about 5 where it becomes
    // obvious. Useful for "are these two too close to tell apart".
    float difference(const color_t &other) const;

    // ── gamut ─────────────────────────────────────────────────────────────────
    // OKLCH can name colours sRGB cannot show, and the conversion clamps each
    // channel independently when it happens, which shifts hue and lightness
    // rather than simply dulling the colour. That matters most where it is least
    // expected: the gamut is lopsided per hue, so rotating a hue at constant
    // chroma walks in and out of it.

    // Whether this colour survives the trip to sRGB intact. Always true for a
    // colour authored as RGB or HSV, which cannot name anything unshowable.
    bool in_gamut() const;

    // The same colour with only as much chroma as sRGB can carry at its
    // lightness and hue, which is the mapping that preserves what a reader
    // actually identifies the colour by. Already-showable colours are returned
    // unchanged, as are RGB and HSV ones.
    color_t fit() const;

    // The chroma ceiling at a given lightness and hue, found by bisection.
    static uint8_t max_chroma(uint8_t l, uint8_t h);

    // ── generating a palette ──────────────────────────────────────────────────
    // Everything below reads the colour in OKLCH first, so it works off a
    // palette entry or a pixel as readily as off a colour that was authored
    // there - and everything it hands back is fitted, because a generated colour
    // that the screen cannot show is of no use to anyone. What comes out is
    // therefore OKLCH whatever went in.

    // Rotate the hue, wrapping. Where with_component sets an absolute hue, this
    // moves relative to the one it has.
    color_t rotate(int counts) const;

    // Move the chroma - OKLCH's c, or HSV's s. lighten's opposite number for the
    // other axis a colour has. Clamps; a negative amount desaturates.
    color_t saturate(int amount) const;

    // The colour-wheel scheme around this colour, this colour first, written into
    // `out`. Returns how many were written, never more than max_harmony. Takes
    // the scheme as an int because it arrives from a binding: an unrecognised one
    // has to be caught before it is an enum, not after.
    static constexpr int max_harmony = 4;
    int harmony(int scheme, color_t *out) const;

    // A tonal ladder: `count` colours at evenly spaced lightness from black to
    // white, holding this colour's hue and chroma. The ends fit down to almost
    // nothing on their own, which is what a tonal palette looks like. `out` holds
    // count colours.
    void tones(color_t *out, int count) const;

    // This colour moved along its lightness until it reaches `ratio` contrast
    // against `background`, holding hue and chroma. Returns the receiver
    // untouched when it already clears the ratio. When neither end of the
    // lightness scale can reach it - a saturated mid-tone background does this -
    // returns the most readable colour available rather than raising, because
    // that is what a caller can actually use.
    color_t readable_on(const color_t &background, float ratio) const;

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
  //
  // Each takes ints and handles its own out-of-range values, so a computed
  // component cannot silently become a different one. A hue wraps, because that
  // is what going round a wheel means; everything else clamps, the same way the
  // arithmetic does.

  // r, g, b: 0-255.
  class rgb_color_t : public color_t {
  public:
    rgb_color_t(int r, int g, int b, int a);
  };

  // h, s, v: 0-255 each; hue is 256 counts to a full turn, and wraps.
  class hsv_color_t : public color_t {
  public:
    hsv_color_t(int h, int s, int v, int a);
  };

  // Recover straight components from a premultiplied word - what a framebuffer
  // holds. Exact at full alpha, which is the case that matters since a screen is
  // opaque; below that only what the premultiplied byte retained can come back
  // (at alpha 10 a channel was stored in 7 of 255 levels). Alpha 0 carries no
  // colour at all - every colour premultiplies to zero - so it reads back as
  // transparent black rather than pretending otherwise.
  rgb_color_t color_from_premul(pixel_t premul);

  // ── ramps ───────────────────────────────────────────────────────────────────
  // Sample `count` colours along a list of (position, colour) stops, filling
  // `out`. Positions are 0-1 offsets, clamped and forced non-decreasing per SVG;
  // the spread past either end is pad.
  //
  // Each segment interpolates through color_t::mix, so a ramp blends the way two
  // colours blend anywhere else: through the components its two ends were
  // authored with when they share a space - two OKLCH stops ramp through OKLCH,
  // which is the point - and through sRGB when they do not.
  //
  // The work happens in the output's own index domain rather than in 0-1 floats,
  // which is what makes each stop land exactly on an entry and come back
  // bit-exact, and what leaves the padded ends costing no interpolation at all.
  //
  // Two overloads because a brush wants premultiplied words for its lookup table
  // and a caller building a palette wants colours; the sampling is the same.
  static constexpr int ramp_max_stops = 16;

  void sample_ramp(color_t *out, int count, const float *positions,
                   const color_t *stops, int n);
  void sample_ramp(pixel_t *out, int count, const float *positions,
                   const color_t *stops, int n);

  // l, c, h: 0-255 each. Not CSS OKLCH units - l covers 0-1 lightness, c covers
  // 0-0.35 chroma, and h is 256 counts to a full turn (so 250 is 352 degrees).
  // Bytes throughout, so a lighten() step means the same thing in every space.
  class oklch_color_t : public color_t {
  public:
    oklch_color_t(int l, int c, int h, int a);
  };

}
