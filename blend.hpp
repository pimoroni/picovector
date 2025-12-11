#pragma once

#include <stdint.h>
#ifndef PICO
#define __not_in_flash_func(v) v
#endif

// TODO: this function probably doesn't belong here...?
inline uint32_t __not_in_flash_func(_make_col)(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
  return __builtin_bswap32((r << 24) | (g << 16) | (b << 8) | a);
}

inline uint32_t _make_col_hsv(uint h, uint s, uint v, uint8_t a = 255) {
  int region = h / 60;
  int remainder = (h - (region * 60)) * 255 / 60;

  int p = (v * (255 - s)) / 255;
  int q = (v * (255 - (s * remainder) / 255)) / 255;
  int t = (v * (255 - (s * (255 - remainder)) / 255)) / 255;

  uint8_t r, g, b;
  switch (region) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default:
      r = v; g = p; b = q; break;
  }
  return _make_col(r, g, b, a);
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

inline uint32_t _make_col_oklch(uint l_in, uint c_in, uint h_in, uint8_t a_in = 255) {
  // Normalise to OKLCH ranges
  float L = (float)l_in / 255.0f;   // 0–1

  float t = (float)c_in / 255.0f;      // 0–1
  // Slightly compress the top end: more resolution at low/mid chroma
  t = 1.0f - (1.0f - t) * (1.0f - t);  // simple quadratic ease-out
  const float OKLCH_MAX_CHROMA = 0.35f;
  float C = t * OKLCH_MAX_CHROMA;

  // Wrap hue and convert to radians
  if (h_in < 0) {
    h_in %= 360;
    if (h_in < 0) h_in += 360;
  }
  h_in %= 360;
  float h = (float)h_in * (float)M_PI / 180.0f;

  // OKLCH → OKLab
  float a = C * cosf(h);
  float b = C * sinf(h);

  // OKLab → LMS (non-linear)
  float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
  float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
  float s_ = L - 0.0894841775f * a - 1.2914855480f * b;

  // Cube to get linear LMS
  l_ = l_ * l_ * l_;
  m_ = m_ * m_ * m_;
  s_ = s_ * s_ * s_;

  // LMS → linear sRGB
  float r_lin =  4.0767416621f * l_ - 3.3077115913f * m_ + 0.2309699292f * s_;
  float g_lin = -1.2684380046f * l_ + 2.6097574011f * m_ - 0.3413193965f * s_;
  float b_lin = -0.0041960863f * l_ - 0.7034186147f * m_ + 1.7076147010f * s_;

  // Linear → sRGB
  float r_srgb = srgb_encode(r_lin);
  float g_srgb = srgb_encode(g_lin);
  float b_srgb = srgb_encode(b_lin);

  // Convert to 0–255
  int ri = (int)(r_srgb * 255.0f + 0.5f);
  int gi = (int)(g_srgb * 255.0f + 0.5f);
  int bi = (int)(b_srgb * 255.0f + 0.5f);

  // Clamp to valid byte range
  if (ri < 0) ri = 0; else if (ri > 255) ri = 255;
  if (gi < 0) gi = 0; else if (gi > 255) gi = 255;
  if (bi < 0) bi = 0; else if (bi > 255) bi = 255;

  return _make_col(ri, gi, bi, a_in);
}


inline uint32_t __not_in_flash_func(_r)(const uint32_t *c) {uint8_t *p = (uint8_t*)c; return p[0];}
inline uint32_t __not_in_flash_func(_g)(const uint32_t *c) {uint8_t *p = (uint8_t*)c; return p[1];}
inline uint32_t __not_in_flash_func(_b)(const uint32_t *c) {uint8_t *p = (uint8_t*)c; return p[2];}
inline uint32_t __not_in_flash_func(_a)(const uint32_t *c) {uint8_t *p = (uint8_t*)c; return p[3];}

inline void __not_in_flash_func(_r)(const uint32_t *c, uint8_t r) {uint8_t *p = (uint8_t*)c; p[0] = r;}
inline void __not_in_flash_func(_g)(const uint32_t *c, uint8_t g) {uint8_t *p = (uint8_t*)c; p[1] = g;}
inline void __not_in_flash_func(_b)(const uint32_t *c, uint8_t b) {uint8_t *p = (uint8_t*)c; p[2] = b;}
inline void __not_in_flash_func(_a)(const uint32_t *c, uint8_t a) {uint8_t *p = (uint8_t*)c; p[3] = a;}

// TODO: consider making all images pre-multiplied alpha

// note: previously we had blend paths using the rp2 interpolators but these
// turn out to be slower due to mmio access times, often taking around 40%
// longer to do the same work

/*

  blending functions

*/

// blends a source rgba pixel over a destination rgba pixel
// (~30 cycles per pixel)
static inline __attribute__((always_inline))
void _blend_rgba_rgba(uint8_t *dst, uint8_t *src) {
  uint8_t sa = src[3]; // source alpha

  if(sa == 255) { // source fully opaque: overwrite
    *(uint32_t *)dst = *(const uint32_t *)src;
    return;
  }
  if(sa == 0) { // source fully transparent: skip
    return;
  }

  // blend r, g, b, and a channels
  dst[0] += (sa * (src[0] - dst[0])) >> 8;
  dst[1] += (sa * (src[1] - dst[1])) >> 8;
  dst[2] += (sa * (src[2] - dst[2])) >> 8;
  dst[3] = sa + ((dst[3] * (255 - sa)) >> 8);
}

// blends a source rgba pixel over a destination rgba pixel with alpha
// (~40 cycles per pixel)
static inline __attribute__((always_inline))
void _blend_rgba_rgba(uint8_t *dst, uint8_t *src, uint8_t a) {
  uint8_t sa = src[3]; // take copy of original source alpha
  uint16_t t = a * sa + 128; // combine source alpha with alpha
  src[3] = (t + (t >> 8)) >> 8;
  _blend_rgba_rgba(dst, src);
  src[3] = sa; // restore source alpha
}

// blends one rgba source pixel over a horizontal span of destination pixels
static inline __attribute__((always_inline))
void _span_blend_rgba_rgba(uint8_t *dst, uint8_t *src, uint32_t w) {
  while(w--) {
    _blend_rgba_rgba(dst, src);
    dst += 4;
  }
}

// blends one rgba source pixel over a horizontal span of destination pixels with alpha mask
static inline __attribute__((always_inline))
void _span_blend_rgba_rgba_masked(uint8_t *dst, uint8_t *src, uint8_t *m, uint32_t w) {
  uint8_t sa = src[3]; // take copy of original source alpha
  while(w--) {
    uint16_t t = *m * sa + 128; // combine source alpha with mask alpha
    src[3] = (t + (t >> 8)) >> 8;
    _blend_rgba_rgba(dst, src);
    dst += 4;
    m++;
  }
  src[3] = sa; // revert the supplied value back
}

/*

  blitting functions

*/

// blends a horizontal run of rgba source pixels onto the corresponding destination pixels
static inline __attribute__((always_inline))
void _span_blit_rgba_rgba(uint8_t *dst, uint8_t *src, uint w, uint8_t a) {
  while(w--) {
    _blend_rgba_rgba(dst, src, a);
    dst += 4;
    src += 4;
  }
}

// blends a horizontal run of rgba source pixels from a palette onto the corresponding destination pixels
static inline __attribute__((always_inline))
void _span_blit_rgba_rgba(uint8_t *dst, uint8_t *src, uint8_t *pal, uint w, uint8_t a) {
  while(w--) {
    _blend_rgba_rgba(dst, &pal[*src << 2], a);
    dst += 4;
    src++;
  }
}

static inline __attribute__((always_inline))
void _span_scale_blit_rgba_rgba(uint8_t *dst, uint8_t *src, uint x, int step, uint w, uint8_t a) {
  while(w--) {
    _blend_rgba_rgba(dst, src + ((x >> 16) << 2), a);
    dst += 4;
    x += step;
  }
}

static inline __attribute__((always_inline))
void _span_scale_blit_rgba_rgba(uint8_t *dst, uint8_t *src, uint8_t *pal, uint x, int step, uint w, uint8_t a) {
  while(w--) {
    uint8_t i = *(src + (x >> 16));
    _blend_rgba_rgba(dst, &pal[i << 2], a);
    dst += 4;
    x += step;
  }
}