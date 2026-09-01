#pragma once

#include "picovector.hpp"
#include "mat3.hpp"
#include "image.hpp"
#include "blend.hpp"
#include "color.hpp"
#include "util.hpp"   // luminance()

namespace picovector {

  // _blend_spans/_blend_masked_spans (blend the shared span buffer with a brush,
  // dispatching to its batch func) are declared in image.hpp, next to the buffer
  // and the _emit_span that fills it.

  // image_t::alpha() is the global alpha everything blended *into* that image is
  // weighted by, so every brush that paints a new colour folds it into the source
  // pixel. A brush that rewrites the pixel it just read (the effect brushes) does
  // not: it mutates the buffer rather than compositing onto it, and carries its
  // own strength instead.
  //
  // Use this where the source colour is invariant across the batch, so the fold
  // happens once. Where the source varies per pixel, read alpha() once outside
  // the loop and apply _premul_mul_alpha only when it isn't 255, so the opaque
  // case stays a plain blend.
  static inline pixel_t fold_target_alpha(image_t *target, pixel_t src) {
    uint32_t a = target->alpha();
    return a == 255u ? src : _premul_mul_alpha(src, a);
  }

  class gradient_brush_t;
  class fractal_brush_t;

  class brush_t {
  public:
    // Composite a whole list of pre-clipped spans in one call: blend_spans a solid
    // batch, blend_masked_spans a coverage-masked (AA) batch. Both read the shared
    // buffer via _spans()/_masked_spans(). Every draw goes through here - the draw
    // methods fill the span buffer and call _blend_spans/_blend_masked_spans.
    virtual void blend_spans(image_t *target, int i0, int i1, int step) = 0;
    virtual void blend_masked_spans(image_t *target, int i0, int i1, int step) = 0;

    // Fold the shape's transform into the brush's own coordinate space, so a
    // brush with geometry (e.g. a gradient) moves with the shape it fills.
    // Called by render() before flushing; no-op for brushes without geometry.
    virtual void set_render_transform(mat3_t *transform) { (void)transform; }

    // A checked downcast, so a caller holding a brush_t can reach the gradient's
    // geometry without RTTI. Costs one vtable entry and nothing per object.
    virtual gradient_brush_t *as_gradient() { return nullptr; }
    virtual fractal_brush_t *as_fractal() { return nullptr; }
  };

  class color_brush_t : public brush_t {
  public:
    pixel_t c;

    color_brush_t(const color_t& c);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // Window / erase brush: lerps the destination toward a premultiplied target
  // colour by shape coverage — dst = lerp(dst, tint, coverage). The default tint
  // is fully transparent, making it a plain eraser (dst-out). Pass a colour to
  // punch a translucent "window" of that colour in a single pass, with AA edges
  // that blend against the background (an opaque tint behaves like a normal fill).
  // Zero-coverage pixels are left exactly untouched. With a transparent tint
  // this is bit-identical to a dst-out erase.
  class transparent_brush_t : public brush_t {
  public:
    pixel_t tint; // target colour; 0 == fully transparent (erase)

    transparent_brush_t();                 // erase (fully transparent)
    transparent_brush_t(const color_t &c); // lerp destination toward colour c
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  class pattern_brush_t : public brush_t {
  public:
    uint8_t p[8];
    pixel_t c1;
    pixel_t c2;

    pattern_brush_t(const color_t& c1, const color_t& c2, uint8_t pattern_index);
    pattern_brush_t(const color_t& c1, const color_t& c2, uint8_t *pattern);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // The widest of the small brushes, and the one that would spill first: it holds
  // two colours, so keeping them as pixels rather than color_t is what keeps a
  // pattern brush inside a single 32-byte GC block.
  static_assert(sizeof(pattern_brush_t) <= 32, "pattern_brush_t now costs two GC blocks");

  class image_brush_t : public brush_t {
  public:
    image_t *src;
    mat3_t inverse_transform;  // device pixels -> image space (incl. shape transform)
    mat3_t base_inverse;       // device -> image for the brush's own transform only
    filter_t filter;

    image_brush_t(image_t *src, filter_t filter = NEAREST);
    image_brush_t(image_t *src, mat3_t *transform, filter_t filter = NEAREST);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
    void set_render_transform(mat3_t *transform) override;
  };

  enum gradient_type_t {
    GRADIENT_LINEAR  = 0, // colour runs along the p1->p2 axis
    GRADIENT_RADIAL  = 1, // colour runs outward from p1, reaching the last stop at |p2-p1|
    GRADIENT_CONICAL = 2  // colour sweeps around p1, starting in the p1->p2 direction
  };



  // Mosaic brush: replaces the shape's area with the target content sampled at
  // an integer block grid (top-left of each `size`x`size` block).
  class pixelate_brush_t : public brush_t {
  public:
    int size;

    pixelate_brush_t(int size);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };


  // Box-blur brush: replaces the shape's area with a (2*radius+1) box average of
  // the target content behind it. Single-pass in place, so it reads some
  // already-written pixels (mild vertical softening); keep radius small.
  class blur_brush_t : public brush_t {
  public:
    int radius;

    blur_brush_t(int radius);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };


  // Lighten/darken brush: adds a signed amount to each RGB channel of the target
  // content behind the shape (positive lightens, negative darkens), clamped.
  class brightness_brush_t : public brush_t {
  public:
    int amount;

    brightness_brush_t(int amount);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // Greyscale brush: replaces the shape's content with its (green-biased)
  // luminance. This is image.monochrome() as a brush - the filter method just
  // fills the image bounds with it.
  class monochrome_brush_t : public brush_t {
  public:
    monochrome_brush_t();
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // Ordered-dither filter as a brush: 4-level green-biased palette via a
  // screen-aligned 4x4 Bayer matrix. image.dither() fills bounds with it.
  class dither_brush_t : public brush_t {
  public:
    dither_brush_t();
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // ── per-pixel colour filters (each also image.<name>()) ─────────────────────

  // Photonegative: rgb -> 255 - rgb (alpha kept).
  class invert_brush_t : public brush_t {
  public:
    invert_brush_t();
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // Two-level threshold on luminance: <= level -> lo, else hi (premultiplied).
  class threshold_brush_t : public brush_t {
  public:
    int level;
    pixel_t lo, hi;
    threshold_brush_t(int level, const color_t &lo, const color_t &hi);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // Push each channel away from (amount>0) or toward (amount<0) its luminance.
  // amount == -256 is a full greyscale; factor is Q8 (256 = identity).
  class saturation_brush_t : public brush_t {
  public:
    int factor;
    saturation_brush_t(int amount);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // Expand (amount>0) / compress (amount<0) each channel around mid-grey (128).
  // factor is Q8 (256 = identity).
  class contrast_brush_t : public brush_t {
  public:
    int factor;
    contrast_brush_t(int amount);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // Map luminance onto a shadow->highlight two-colour ramp (sepia etc). LUT
  // built in the ctor.
  class duotone_brush_t : public brush_t {
  public:
    pixel_t lut[256];
    duotone_brush_t(const color_t &shadow, const color_t &highlight);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // CRT tube: darken every `spacing`-th row by `darkness` (0..255) plus a rounded
  // corner/edge falloff.
  class crt_brush_t : public brush_t {
  public:
    int spacing, darkness, str;   // str: Q8 overall-darkening scale (256 = full)
    crt_brush_t(int spacing, int darkness, int str = 256);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // Gentle pixel grid: darken every `spacing`-th row and column by `darkness`.
  class grid_brush_t : public brush_t {
  public:
    int spacing, darkness;
    grid_brush_t(int spacing, int darkness);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // Darken by distance from the image (target-bounds) centre, `strength` 0..255.
  class vignette_brush_t : public brush_t {
  public:
    int strength;
    vignette_brush_t(int strength);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // ── playful / retro filters (each also image.<name>()) ──────────────────────

  // Deterministic per-pixel film grain: +/- up to `amount`, keyed on (x, y).
  class noise_brush_t : public brush_t {
  public:
    int amount, frame;
    // interval is the grain refresh period in ms (0 = static)
    noise_brush_t(int amount, int interval);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // VHS glitch: on hashed y-bands (frequency from `amount`), rotate the colour
  // channels for a torn-signal look.
  class glitch_brush_t : public brush_t {
  public:
    int amount;
    uint32_t t;   // PV_TICKS captured at construction (animation phase)
    glitch_brush_t(int amount);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // ── artwork / painterly ─────────────────────────────────────────────────────

  // Oil paint: replace each pixel with the dominant colour in a `radius`
  // neighbourhood - reads the target, so chunk-buffered like the blur brush.
  class oilpaint_brush_t : public brush_t {
  public:
    int radius, strength, sstep;
    oilpaint_brush_t(int radius, int strength);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // ── retro computing ─────────────────────────────────────────────────────────

  // CRT phosphor: monochrome glow toward `tint` (green/amber terminals).
  class phosphor_brush_t : public brush_t {
  public:
    pixel_t tint;
    phosphor_brush_t(const color_t &tint);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // Ordered dither to an arbitrary palette. An 8x8 Bayer pattern nudges each
  // pixel across quantisation boundaries so flat areas dither between the
  // nearest palette colours. A 4KB cube (top 4 bits per channel -> palette
  // index) turns the per-pixel nearest search into one lookup, so cost is flat
  // regardless of palette size. strength scales the dither spread (128 = the
  // palette's mean spacing).
  class palette_dither_brush_t : public brush_t {
  public:
    uint32_t pal[64];
    int n;
    int spread;
    uint8_t *cube;    // 4096 entries, 12-bit rgb -> palette index

    palette_dither_brush_t(const uint32_t *colors, int n, int strength);
    ~palette_dither_brush_t();
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // ── futuristic / sci-fi ─────────────────────────────────────────────────────

  // Night vision: amplify to green + grain + edge darkening.
  class nightvision_brush_t : public brush_t {
  public:
    int frame;   // PV_TICKS-derived, animates the grain
    nightvision_brush_t();
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };

  // Chromatic aberration: shift R left / B right by `offset` px - reads the row,
  // so chunk-buffered.
  class chromatic_brush_t : public brush_t {
  public:
    int offset;
    chromatic_brush_t(int offset);
    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
  };


  // SVG-style linear/radial gradient, plus a conical (angular sweep) one for
  // gauges and dials. Geometry (p1, p2) lives in the gradient's own coordinate
  // space; `transform` maps that space onto device pixels (for SVG
  // objectBoundingBox the caller maps the unit square onto the shape bbox).
  //
  // What p1/p2 mean depends on the type:
  //   LINEAR   p1 and p2 are the ends of the axis.
  //   RADIAL   p1 is the centre, |p2 - p1| the radius.
  //   CONICAL  p1 is the centre and the p1->p2 direction is where the ramp
  //            starts; the distance is unused. Stop offsets are fractions of a
  //            full turn, clockwise, so a 270 degree gauge puts its stops in
  //            0..0.75. Angles run clockwise from the start direction, matching
  //            shape.arc()/pie(), so a start direction of straight up lines up
  //            with arc(cx, cy, r1, r2, 0, 270) without any fixup.
  class gradient_brush_t : public brush_t {
  public:
    // However many the shared ramp sampler can carve up.
    static constexpr int max_stops = ramp_max_stops;

    gradient_type_t type;
    vec2_t p1, p2;             // gradient endpoints in gradient coordinate space
    float dir_c, dir_s;        // normalised p1->p2, the conical sweep's zero angle
    mat3_t inverse_transform;  // device pixels -> gradient coordinate space (incl. shape transform)
    mat3_t base_inverse;       // device -> gradient for the brush's own transform only
    pixel_t lut[256];          // colours sampled along the gradient

    // `lut` weighted by the target's global alpha, so the pixel loop reads a
    // table that already carries it. The stops are gone by the end of the
    // constructor and premultiplying is lossy, so `lut` itself is never touched -
    // it is the only copy. Nothing rewrites it after construction (geometry()
    // leaves it alone, and ramp() is fractal-only), so the alpha it was folded at
    // is the whole of the invalidation key.
    pixel_t faded[256];
    uint8_t faded_alpha = 255;   // 255 never reads `faded`, so this always folds first

    // positions are 0..1 stop offsets. The stops keep their colour_t so each
    // segment can interpolate in the space its two ends were authored in.
    // `type` is an int, not the enum: it arrives from a binding and an
    // out-of-range value has to be caught before it is one, not after.
    gradient_brush_t(int type, float x1, float y1, float x2, float y2,
                     const float *positions, const color_t *stops, int stop_count,
                     mat3_t *transform);

    // Move the gradient without rebuilding its lookup table. The stops are what
    // the table encodes, so an animated gradient that only changes where it sits
    // has no reason to pay for a rebuild - which at ~440us a time is most of the
    // cost of using one.
    void geometry(float x1, float y1, float x2, float y2, mat3_t *transform);

    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
    void set_render_transform(mat3_t *transform) override;
    gradient_brush_t *as_gradient() override { return this; }

  private:
    // The table to read for this target, weighting `lut` by its global alpha only
    // when that changes. nullptr when the alpha is 0 and there is nothing to draw.
    const pixel_t *ramp_for(image_t *target);
  };

  // ── procedural ──────────────────────────────────────────────────────────────

  // Fractal (fBm) value noise mapped through a colour ramp. See fractal.cpp.
  //
  // The field has an intrinsic cell size, composed with a placement transform the
  // way an image brush composes one with its pixel grid: an identity placement
  // gives cells of `scale` device pixels, and a placement that only rotates or
  // translates leaves feature size alone. Placement translation is in device pixels.
  //
  // Ramp stop positions are area fractions of the field, measured once at
  // construction, so neither geometry() nor resize() touches the ramp.
  class fractal_brush_t : public brush_t {
  public:
    static constexpr int max_octaves = 4;
    static constexpr int max_stops = ramp_max_stops;

    int octaves;
    int weight[max_octaves];   // totals 256, so the fractal sum normalises with a shift
    int repeat;                // tile period in cells, a power of two
    int wrap[max_octaves];     // per-octave cell mask, (repeat << o) - 1
    uint32_t seed;             // chooses the corner hash; same seed, same field
    uint8_t perm[256];         // this brush's corner hash permutation
    float cell;                // device pixels per cell, before placement
    mat3_t placement;          // field -> device, as supplied
    mat3_t inverse_transform;  // device pixels -> field space (incl. shape transform)
    mat3_t base_inverse;       // device -> field for cell size and placement
    uint8_t equalised[256];    // noise value -> fraction of the field below it
    pixel_t lut[256];          // the ramp, indexed by fractal value

    // `repeat` is the tile period in cells, rounded down to a power of two. Every
    // octave has to wrap on the same spatial period and the finest is
    // 2^(octaves-1) times denser, so the usable maximum is 256 >> (octaves - 1);
    // 0 asks for that maximum.
    fractal_brush_t(float scale, int octaves, float persistence, int repeat,
                    uint32_t seed, mat3_t *transform);

    // Recolour. positions are 0..1 area fractions; two stops sharing a position
    // are a hard edge, spaced stops a soft one.
    void ramp(const float *positions, const color_t *stops, int stop_count);

    // Place the field, and set its cell size. Both leave the ramp alone.
    void geometry(mat3_t *transform);
    void resize(float scale);

    void blend_spans(image_t *target, int i0, int i1, int step) override;
    void blend_masked_spans(image_t *target, int i0, int i1, int step) override;
    void set_render_transform(mat3_t *transform) override;
    fractal_brush_t *as_fractal() override { return this; }

  private:
    void measure();
    void rebuild();   // cell size and placement -> base_inverse
  };

}