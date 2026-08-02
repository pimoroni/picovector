#include "color.hpp"
#include "blend.hpp"

namespace picovector {

  static inline uint8_t clamp_byte(int v) {
    if(v < 0) return 0;
    if(v > 255) return 255;
    return (uint8_t)v;
  }

  static inline pixel_t premultiply(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t rp = (r * a) / 255;
    uint8_t gp = (g * a) / 255;
    uint8_t bp = (b * a) / 255;
    return __builtin_bswap32((rp << 24) | (gp << 16) | (bp << 8) | a);
  }

  static void hsv_to_srgb(uint8_t h, uint8_t s, uint8_t v, uint8_t &r, uint8_t &g, uint8_t &b) {
    int hs = h * 6;             // six hue regions of 256
    int region = hs >> 8;       // 0..5, since 255 * 6 = 1530
    int remainder = hs & 0xff;  // position within the region, 0..255

    int p = (v * (255 - s)) / 255;
    int q = (v * (255 - (s * remainder) / 255)) / 255;
    int t = (v * (255 - (s * (255 - remainder)) / 255)) / 255;

    switch (region) {
      case 0:  r = v; g = t; b = p; break;
      case 1:  r = q; g = v; b = p; break;
      case 2:  r = p; g = v; b = t; break;
      case 3:  r = p; g = q; b = v; break;
      case 4:  r = t; g = p; b = v; break;
      default: r = v; g = p; b = q; break;
    }
  }


  static inline float clamp01(float x) {
      if (x < 0.0f) return 0.0f;
      if (x > 1.0f) return 1.0f;
      return x;
  }


  static inline float srgb_encode(float x) {
      x = clamp01(x);
      if (x <= 0.0031308f) {
          return 12.92f * x;
      } else {
          return 1.055f * powf(x, 1.0f / 2.4f) - 0.055f;
      }
  }

  static void oklch_to_srgb(uint8_t l, uint8_t c, uint8_t h, uint8_t &r, uint8_t &g, uint8_t &b) {
    // Normalise to OKLCH ranges
    float L = (float)l / 255.0f;   // 0-1

    const float OKLCH_MAX_CHROMA = 0.35f;
    float C = ((float)c / 255.0f) * OKLCH_MAX_CHROMA;

    // byte hue -> radians directly; 256 counts = a full turn (2*PI)
    float hs = (float)h * (PV_PI / 128.0f);

    // OKLCH -> OKLab
    float a_ = C * cosf(hs);
    float b_ = C * sinf(hs);

    // OKLab -> LMS (non-linear)
    float l_ = L + 0.3963377774f * a_ + 0.2158037573f * b_;
    float m_ = L - 0.1055613458f * a_ - 0.0638541728f * b_;
    float s_ = L - 0.0894841775f * a_ - 1.2914855480f * b_;

    // Cube to get linear LMS
    l_ = l_ * l_ * l_;
    m_ = m_ * m_ * m_;
    s_ = s_ * s_ * s_;

    // LMS -> linear sRGB
    float r_lin =  4.0767416621f * l_ - 3.3077115913f * m_ + 0.2309699292f * s_;
    float g_lin = -1.2684380046f * l_ + 2.6097574011f * m_ - 0.3413193965f * s_;
    float b_lin = -0.0041960863f * l_ - 0.7034186147f * m_ + 1.7076147010f * s_;

    // Linear -> sRGB, then to 0-255
    r = clamp_byte((int)(srgb_encode(r_lin) * 255.0f + 0.5f));
    g = clamp_byte((int)(srgb_encode(g_lin) * 255.0f + 0.5f));
    b = clamp_byte((int)(srgb_encode(b_lin) * 255.0f + 0.5f));
  }

  void color_t::author(color_space_t space, uint8_t c0, uint8_t c1, uint8_t c2, uint8_t a) {
    _space = space;
    _c0 = c0; _c1 = c1; _c2 = c2; _a = a;

    switch(space) {
      case COLOR_HSV:   hsv_to_srgb(c0, c1, c2, _sr, _sg, _sb); break;
      case COLOR_OKLCH: oklch_to_srgb(c0, c1, c2, _sr, _sg, _sb); break;
      default:          _sr = c0; _sg = c1; _sb = c2; break;
    }

    _p = premultiply(_sr, _sg, _sb, _a);
  }

  rgb_color_t::rgb_color_t(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    author(COLOR_RGB, r, g, b, a);
  }

  hsv_color_t::hsv_color_t(uint8_t h, uint8_t s, uint8_t v, uint8_t a) {
    author(COLOR_HSV, h, s, v, a);
  }

  oklch_color_t::oklch_color_t(uint8_t l, uint8_t c, uint8_t h, uint8_t a) {
    author(COLOR_OKLCH, l, c, h, a);
  }

  // ── arithmetic ──────────────────────────────────────────────────────────────

  // Which authored component means lightness: v in HSV, l in OKLCH. RGB has no
  // single one, so its callers act on all three channels instead.
  static inline int lightness_slot(color_space_t space) {
    switch(space) {
      case COLOR_HSV:   return 2;   // v
      case COLOR_OKLCH: return 0;   // l
      default:          return -1;
    }
  }

  // Which authored component is a hue, wrapping at 256, or -1 for none.
  static inline int hue_slot(color_space_t space) {
    switch(space) {
      case COLOR_HSV:   return 0;
      case COLOR_OKLCH: return 2;
      default:          return -1;
    }
  }

  // Interpolate a byte pair, t 0..255. Rounds away from `from` so that t == 255
  // lands exactly on `to` for a falling pair as well as a rising one.
  static inline uint8_t lerp_byte(int from, int to, int t) {
    int d = to - from;
    return clamp_byte(from + ((d * t + (d < 0 ? -127 : 127)) / 255));
  }

  // Interpolate a hue the short way round: 256 counts is a full turn, so a
  // difference of more than half a turn goes the other way instead.
  static inline uint8_t lerp_hue(int from, int to, int t) {
    int d = to - from;
    if(d > 128) d -= 256; else if(d < -128) d += 256;
    return (uint8_t)((from + ((d * t + (d < 0 ? -127 : 127)) / 255)) & 0xff);
  }

  color_t color_t::lighten(int amount) const {
    uint8_t c[3] = { _c0, _c1, _c2 };
    int slot = lightness_slot(_space);

    if(slot < 0) {
      for(int i = 0; i < 3; i++) c[i] = clamp_byte(c[i] + amount);
    } else {
      c[slot] = clamp_byte(c[slot] + amount);
    }

    color_t out;
    out.author(_space, c[0], c[1], c[2], _a);
    return out;
  }

  color_t color_t::scale(int percent) const {
    uint8_t c[3] = { _c0, _c1, _c2 };
    int slot = lightness_slot(_space);

    if(slot < 0) {
      for(int i = 0; i < 3; i++) c[i] = clamp_byte((c[i] * percent + 50) / 100);
    } else {
      c[slot] = clamp_byte((c[slot] * percent + 50) / 100);
    }

    color_t out;
    out.author(_space, c[0], c[1], c[2], _a);
    return out;
  }

  color_t color_t::with_alpha(int a) const {
    color_t out;
    out.author(_space, _c0, _c1, _c2, clamp_byte(a));
    return out;
  }

  color_t color_t::with_component(int index, int value) const {
    uint8_t c[3] = { _c0, _c1, _c2 };
    if(index >= 0 && index < 3) c[index] = clamp_byte(value);

    color_t out;
    out.author(_space, c[0], c[1], c[2], _a);
    return out;
  }

  color_t color_t::mix(const color_t &other, int t) const {
    color_t out;
    t = clamp_byte(t);
    uint8_t alpha = lerp_byte(_a, other._a, t);

    if(_space != other._space) {
      // No shared set of components; interpolate what both sides do have.
      out.author(COLOR_RGB, lerp_byte(_sr, other._sr, t),
                            lerp_byte(_sg, other._sg, t),
                            lerp_byte(_sb, other._sb, t), alpha);
      return out;
    }

    int hue = hue_slot(_space);
    uint8_t c[3];
    for(int i = 0; i < 3; i++) {
      c[i] = (i == hue) ? lerp_hue(component(i), other.component(i), t)
                        : lerp_byte(component(i), other.component(i), t);
    }

    out.author(_space, c[0], c[1], c[2], alpha);
    return out;
  }

  color_t color_t::over(const color_t &background) const {
    return color_from_premul(blend_over_premul(background._p, _p));
  }

  rgb_color_t color_from_premul(pixel_t premul) {
    uint32_t a = (premul >> 24) & 0xffu;
    if(a == 0) return rgb_color_t(0, 0, 0, 0);
    auto straight = [a](uint32_t c) -> uint8_t {
      uint32_t v = (c * 255u + a / 2u) / a;   // rounded, and exact when a == 255
      return (uint8_t)(v > 255u ? 255u : v);
    };
    return rgb_color_t(straight(premul & 0xffu),
                       straight((premul >> 8) & 0xffu),
                       straight((premul >> 16) & 0xffu),
                       (uint8_t)a);
  }
}
