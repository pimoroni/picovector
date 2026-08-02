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

  // The other direction, for the paths that have to get back to light: reading a
  // colour's OKLCH components, and weighting channels for a luminance.
  static inline float srgb_decode(float x) {
      if (x <= 0.04045f) {
          return x / 12.92f;
      } else {
          return powf((x + 0.055f) / 1.055f, 2.4f);
      }
  }

  // Chroma is stored as a fraction of this. 0.35 comfortably covers the sRGB
  // gamut, whose most saturated colour sits near 0.32, so the ceiling costs
  // nothing and the byte spends its range where colours actually are.
  static constexpr float OKLCH_MAX_CHROMA = 0.35f;

  // `clipped`, when asked for, reports that a linear channel fell outside 0-1
  // and was truncated - the colour named does not exist in sRGB, and what comes
  // back has a shifted hue and lightness rather than a merely duller version of
  // it. The slack is a fraction of a byte's worth of light, so clipping too
  // small to see does not count.
  static void oklch_to_srgb(uint8_t l, uint8_t c, uint8_t h, uint8_t &r, uint8_t &g, uint8_t &b,
                            bool *clipped = nullptr) {
    // Normalise to OKLCH ranges
    float L = (float)l / 255.0f;   // 0-1

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

    if(clipped) {
      // Clipping that cannot move the output byte is not clipping. Half a byte
      // is worth very different amounts of linear light at the two ends - the
      // transfer curve is 13x steeper near black than the channel scale, and far
      // shallower near white - so the two bounds are not symmetric.
      const float slack_low = 0.5f / 255.0f / 12.92f;          // ~0.00015
      const float slack_high = 1.0f - srgb_decode(254.5f / 255.0f);  // ~0.0045

      *clipped = r_lin < -slack_low || r_lin > 1.0f + slack_high
              || g_lin < -slack_low || g_lin > 1.0f + slack_high
              || b_lin < -slack_low || b_lin > 1.0f + slack_high;
    }

    // Linear -> sRGB, then to 0-255
    r = clamp_byte((int)(srgb_encode(r_lin) * 255.0f + 0.5f));
    g = clamp_byte((int)(srgb_encode(g_lin) * 255.0f + 0.5f));
    b = clamp_byte((int)(srgb_encode(b_lin) * 255.0f + 0.5f));
  }

  // sRGB back to OKLab: the inverse of the pipeline above, and the only way a
  // colour that was not authored in OKLCH can report a lightness, a chroma or a
  // hue. L comes back 0-1; a and b are roughly -0.4 to 0.4.
  static void srgb_to_oklab(uint8_t r, uint8_t g, uint8_t b, float &L, float &a, float &bb) {
    float r_lin = srgb_decode((float)r / 255.0f);
    float g_lin = srgb_decode((float)g / 255.0f);
    float b_lin = srgb_decode((float)b / 255.0f);

    // linear sRGB -> LMS
    float l_ = 0.4122214708f * r_lin + 0.5363325363f * g_lin + 0.0514459929f * b_lin;
    float m_ = 0.2119034982f * r_lin + 0.6806995451f * g_lin + 0.1073969566f * b_lin;
    float s_ = 0.0883024619f * r_lin + 0.2817188376f * g_lin + 0.6299787005f * b_lin;

    // Cube root to undo the cube the forward direction applies
    l_ = cbrtf(l_);
    m_ = cbrtf(m_);
    s_ = cbrtf(s_);

    L  = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
    a  = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
    bb = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;
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

  // ── changing space ──────────────────────────────────────────────────────────

  color_t color_t::to_rgb() const {
    if(_space == COLOR_RGB) return *this;
    color_t out;
    out.author(COLOR_RGB, _sr, _sg, _sb, _a);
    return out;
  }

  color_t color_t::to_oklch() const {
    if(_space == COLOR_OKLCH) return *this;

    float L, a, b;
    srgb_to_oklab(_sr, _sg, _sb, L, a, b);

    float C = sqrtf(a * a + b * b);
    // atan2 gives -PI..PI; the byte wraps, so a negative angle needs no fixup
    // beyond the mask.
    int hue = (int)lroundf(atan2f(b, a) * (128.0f / PV_PI)) & 0xff;

    color_t out;
    out.author(COLOR_OKLCH,
               clamp_byte((int)lroundf(L * 255.0f)),
               clamp_byte((int)lroundf((C / OKLCH_MAX_CHROMA) * 255.0f)),
               (uint8_t)hue, _a);
    return out;
  }

  // ── measurement ─────────────────────────────────────────────────────────────

  float color_t::luminance() const {
    return 0.2126f * srgb_decode((float)_sr / 255.0f)
         + 0.7152f * srgb_decode((float)_sg / 255.0f)
         + 0.0722f * srgb_decode((float)_sb / 255.0f);
  }

  float color_t::contrast(const color_t &other) const {
    float lighter = luminance(), darker = other.luminance();
    if(lighter < darker) { float swap = lighter; lighter = darker; darker = swap; }
    return (lighter + 0.05f) / (darker + 0.05f);
  }

  float color_t::difference(const color_t &other) const {
    float L1, a1, b1, L2, a2, b2;
    srgb_to_oklab(_sr, _sg, _sb, L1, a1, b1);
    srgb_to_oklab(other._sr, other._sg, other._sb, L2, a2, b2);

    float dL = L1 - L2, da = a1 - a2, db = b1 - b2;
    return sqrtf(dL * dL + da * da + db * db) * 100.0f;
  }

  // ── gamut ───────────────────────────────────────────────────────────────────

  bool color_t::in_gamut() const {
    // RGB says its channels outright, and HSV is defined in terms of them, so
    // only OKLCH can name a colour the screen cannot show.
    if(_space != COLOR_OKLCH) return true;

    uint8_t r, g, b;
    bool clipped = false;
    oklch_to_srgb(_c0, _c1, _c2, r, g, b, &clipped);
    return !clipped;
  }

  uint8_t color_t::max_chroma(uint8_t l, uint8_t h) {
    uint8_t r, g, b;
    bool clipped = false;

    oklch_to_srgb(l, 255, h, r, g, b, &clipped);
    if(!clipped) return 255;

    // Chroma 0 is a grey at that lightness, which always exists, so the bottom
    // of the search is known good from the outset. Eight steps of the forward
    // conversion, each a couple of trig calls and three powf - fine for building
    // a palette, far too slow to sit in a pixel loop.
    int good = 0, bad = 255;
    while(bad - good > 1) {
      int mid = (good + bad) / 2;
      oklch_to_srgb(l, (uint8_t)mid, h, r, g, b, &clipped);
      if(clipped) bad = mid; else good = mid;
    }
    return (uint8_t)good;
  }

  color_t color_t::fit() const {
    if(_space != COLOR_OKLCH || in_gamut()) return *this;

    color_t out;
    out.author(COLOR_OKLCH, _c0, max_chroma(_c0, _c2), _c2, _a);
    return out;
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

  // ── generating a palette ────────────────────────────────────────────────────

  // An OKLCH colour that the screen can show, which is the only kind worth
  // handing back from a generator.
  static color_t oklch_fitted(int l, int c, int h, uint8_t a) {
    return oklch_color_t(clamp_byte(l), clamp_byte(c), (uint8_t)(h & 0xff), a).fit();
  }

  color_t color_t::rotate(int counts) const {
    color_t base = to_oklch();
    return oklch_fitted(base.l(), base.c(), base.h() + counts, _a);
  }

  color_t color_t::saturate(int amount) const {
    // HSV keeps its own saturation, which is a different axis with a different
    // feel; only RGB has nothing to move and has to be read in OKLCH first.
    if(_space == COLOR_HSV) return with_component(1, _c1 + amount);

    color_t base = to_oklch();
    return oklch_fitted(base.l(), base.c() + amount, base.h(), _a);
  }

  int color_t::harmony(int scheme, color_t *out) const {
    // Offsets around the wheel, in hue counts. 120 degrees is 85.33 counts, so
    // a triad is a third of a degree out - closer than the hue byte can resolve.
    static const int16_t offsets[][3] = {
      { 128,   0,   0 },   // COMPLEMENT: 180
      { 107, 149,   0 },   // SPLIT: 150 and 210
      {  85, 171,   0 },   // TRIAD: 120 and 240
      {  43, 128, 171 },   // TETRAD: 60, 180 and 240
      {  64, 128, 192 },   // SQUARE: 90, 180 and 270
      { -21,  21,   0 },   // ANALOGOUS: either side by 30
    };
    static const int counts[] = { 2, 3, 3, 4, 4, 3 };

    if(scheme < 0 || scheme > SCHEME_ANALOGOUS) scheme = SCHEME_COMPLEMENT;

    color_t base = to_oklch();
    out[0] = base.fit();

    int n = counts[scheme];
    for(int i = 1; i < n; i++) {
      out[i] = oklch_fitted(base.l(), base.c(), base.h() + offsets[scheme][i - 1], _a);
    }
    return n;
  }

  void color_t::tones(color_t *out, int count) const {
    color_t base = to_oklch();
    if(count < 1) return;
    if(count == 1) { out[0] = base.fit(); return; }

    for(int i = 0; i < count; i++) {
      out[i] = oklch_fitted((i * 255 + (count - 1) / 2) / (count - 1), base.c(), base.h(), _a);
    }
  }

  color_t color_t::readable_on(const color_t &background, float ratio) const {
    if(contrast(background) >= ratio) return *this;

    color_t base = to_oklch();
    int chroma = base.c(), hue = base.h();

    color_t darkest = oklch_fitted(0, chroma, hue, _a);
    color_t lightest = oklch_fitted(255, chroma, hue, _a);
    float dark_reach = darkest.contrast(background);
    float light_reach = lightest.contrast(background);

    // A saturated mid-tone background can be out of reach in both directions.
    // The caller wants the most readable thing there is, not an exception.
    if(dark_reach < ratio && light_reach < ratio) {
      return dark_reach > light_reach ? darkest : lightest;
    }

    // Where both ends work, take the shorter walk: the point is a readable
    // colour that still looks like the one asked for.
    bool go_dark = dark_reach >= ratio &&
                   (light_reach < ratio || base.l() < 255 - base.l());

    int reaches = go_dark ? 0 : 255;   // known to clear the ratio
    int falls_short = base.l();        // known not to
    while(reaches - falls_short > 1 || falls_short - reaches > 1) {
      int mid = (reaches + falls_short) / 2;
      if(oklch_fitted(mid, chroma, hue, _a).contrast(background) >= ratio) {
        reaches = mid;
      } else {
        falls_short = mid;
      }
    }
    return oklch_fitted(reaches, chroma, hue, _a);
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
