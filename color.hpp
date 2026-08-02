#pragma once

#include <cstdint>

#include "picovector.hpp"

namespace picovector {

  // A premultiplied packed RGBA word: the framebuffer's pixel format and what
  // every blend consumes. A typedef, not a type, so it changes no signature - it
  // names the role at the boundaries where "a colour" and "a pixel" are easy to
  // confuse. Channel order matches blend.hpp's _r/_g/_b/_a extractors.
  typedef uint32_t pixel_t;

  class color_t {
  public:
    pixel_t _p; // pre-multiplied r, g, b, a

  public:
    virtual ~color_t() = default;
    void premul(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
  };

  class rgb_color_t : public color_t {
    uint8_t _r, _g, _b, _a;

  public:
    rgb_color_t(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    uint8_t r() const { return _r; }
    uint8_t g() const { return _g; }
    uint8_t b() const { return _b; }
    uint8_t a() const { return _a; }
  };

  class hsv_color_t : public color_t {
    uint8_t _h, _s, _v, _a;

  public:
    hsv_color_t(uint8_t h, uint8_t s, uint8_t v, uint8_t a);
    uint8_t h() const { return _h; }
    uint8_t s() const { return _s; }
    uint8_t v() const { return _v; }
    uint8_t a() const { return _a; }
  };

  // Recover straight components from a premultiplied word - what a framebuffer
  // holds. Exact at full alpha, which is the case that matters since a screen is
  // opaque; below that only what the premultiplied byte retained can come back
  // (at alpha 10 a channel was stored in 7 of 255 levels). Alpha 0 carries no
  // colour at all - every colour premultiplies to zero - so it reads back as
  // transparent black rather than pretending otherwise.
  rgb_color_t color_from_premul(pixel_t premul);

  class oklch_color_t : public color_t {
    float _l, _c, _h, _a;

  public:
    oklch_color_t(uint8_t l, uint8_t c, uint8_t h, uint8_t a);
    uint8_t l() const { return _l; }
    uint8_t c() const { return _c; }
    uint8_t h() const { return _h; }
    uint8_t a() const { return _a; }
  };

}