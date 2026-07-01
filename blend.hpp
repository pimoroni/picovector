#pragma once

#include <stdint.h>

#include "color.hpp"

static inline __attribute__((always_inline))
uint32_t _r(const uint32_t c) {return c & 0xffu;}
static inline __attribute__((always_inline))
uint32_t _g(const uint32_t c) {return (c >> 8) & 0xffu;}
static inline __attribute__((always_inline))
uint32_t _b(const uint32_t c) {return (c >> 16) & 0xffu;}
static inline __attribute__((always_inline))
uint32_t _a(const uint32_t c) {return (c >> 24) & 0xffu;}

// takes a premultiplied packed color and applies alpha
//
// SWAR: two channels ride in one 32-bit multiply. Each channel is <= 255 and a
// is <= 255, so a channel*a product is <= 65025 (< 65536) and never carries into
// its neighbour, which sits 16 bits away. This is bit-identical to the scalar
// (chan*a + 128) >> 8 form it replaces, at half the multiplies.
static inline __attribute__((always_inline))
uint32_t _premul_mul_alpha(uint32_t c, uint32_t a) {
  uint32_t rb = ( c        & 0x00ff00ffu) * a;  // R in [0:15],  B in [16:31]
  uint32_t ga = ((c >>  8) & 0x00ff00ffu) * a;  // G in [0:15],  A in [16:31]
  rb = ((rb + 0x00800080u) >> 8) & 0x00ff00ffu;
  ga = ((ga + 0x00800080u) >> 8) & 0x00ff00ffu;
  return rb | (ga << 8);
}

// composites a premultiplied packed source pixel over a packed dst pixel ("over").
// dst is opaque-or-not; the source alpha weights how much dst survives. Because
// the source is premultiplied, the composite is just src + dst*(1-a), and both
// the dst*(1-a) scale and the final add stay in the packed SWAR domain (a valid
// premultiplied result is <= 255 per channel, so the add never carries).
static inline __attribute__((always_inline))
uint32_t blend_over_premul(uint32_t d, uint32_t c) {
  uint32_t a = c >> 24;
  if (a == 0u)   return d;   // source transparent: dst unchanged
  if (a == 255u) return c;   // source opaque: overwrite

  uint32_t inva = 255u - a;
  uint32_t drb = ((( d        & 0x00ff00ffu) * inva + 0x00800080u) >> 8) & 0x00ff00ffu;
  uint32_t dga = ((((d >>  8) & 0x00ff00ffu) * inva + 0x00800080u) >> 8) & 0x00ff00ffu;
  return (( c        & 0x00ff00ffu) + drb)
       | ((((c >> 8) & 0x00ff00ffu) + dga) << 8);
}

static inline __attribute__((always_inline))
uint32_t _premul_mul_alpha_channel(uint32_t c, uint32_t a) {
  return (c * a + 128) >> 8;
}

typedef uint32_t (*blend_func_t)(uint32_t dst, uint32_t r, uint32_t g, uint32_t b, uint32_t a);

// unpacked-args wrapper kept for the sample()-based span paths (blit_span et al)
// that still hand callers r,g,b,a separately. Repacks and defers to the packed
// blend so there's a single "over" implementation to maintain.
static uint32_t blend_func_over(uint32_t dst, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    return blend_over_premul(dst, r | (g << 8) | (b << 16) | (a << 24));
}


// // blends one rgba source pixel over a horizontal span of destination pixels
// static inline __attribute__((always_inline))
// void span_blend_rgba_rgba(uint8_t *dst, uint8_t *src, uint32_t w) {
//   uint8_t sa = src[3];

//   if(sa == 0) return; // source fully transparent; skip

//   if(sa == 255) {     // source fully opaque; overwrite
//     uint32_t *p = (uint32_t*)dst;
//     uint32_t c = *(uint32_t*)src;
//     while(w--) { *p++ = c; }
//     return;
//   }

//   uint8_t r = src[0], g = src[1], b = src[2], a = src[3];
//   while(w--) {
//     blend_rgba_rgba(dst, r, g, b, a);
//     dst += 4;
//   }
// }

// // blends one rgba source pixel over a horizontal span of destination pixels with alpha mask
// static inline __attribute__((always_inline))
// void mask_span_blend_rgba_rgba(uint8_t *dst, uint8_t *src, uint32_t w, uint8_t *m) {
//   uint8_t r = src[0], g = src[1], b = src[2];
//   uint8_t sa = src[3]; // take copy of original source alpha
//   while(w--) {
//     uint16_t t = *m * sa + 128; // combine source alpha with mask alpha
//     uint8_t a = (t + (t >> 8)) >> 8;
//     blend_rgba_rgba(dst, r, g, b, a);
//     dst += 4;
//     m++;
//   }
// }

/*

  blitting functions

*/

// blends a horizontal run of rgba source pixels onto the corresponding destination pixels
// static inline __attribute__((always_inline))
// void _span_blit_rgba_rgba(uint8_t *dst, color_t c, int w, uint8_t a) {
//   while(w--) {
//     uint8_t r = src[0], g = src[1], b = src[2], a = src[3];
//     *dst = blend_func_over(*dst, r, g, b, a);
//     //_blend_rgba_rgba(dst, src, a);
//     dst += 4;
//     src += 4;
//   }
// }

// // blends a horizontal run of rgba source pixels from a palette onto the corresponding destination pixels
// static inline __attribute__((always_inline))
// void _span_blit_rgba_rgba(uint8_t *dst, uint8_t *src, uint8_t *pal, int w, uint8_t a) {
//   while(w--) {
//     uint8_t *col = &pal[*src << 2];
//     uint8_t r = col[0], g = col[1], b = col[2], a = col[3];
//     *dst = blend_rgba_rgba(*dst, r, g, b, a);
//     //_blend_rgba_rgba(dst, &pal[*src << 2], a);
//     dst += 4;
//     src++;
//   }
// }

// static inline __attribute__((always_inline))
// void _span_scale_blit_rgba_rgba(uint8_t *dst, uint8_t *src, int x, int step, int w, uint8_t a) {
//   while(w--) {
//     uint8_t *osrc = src + ((x >> 16) << 2);
//     uint8_t r = osrc[0], g = osrc[1], b = osrc[2], a = osrc[3];
//     blend_rgba_rgba(dst, r, g, b, a);
//     dst += 4;
//     x += step;
//   }
// }

// static inline __attribute__((always_inline))
// void _span_scale_blit_rgba_rgba(uint8_t *dst, uint8_t *src, uint8_t *pal, int x, int step, int w, uint8_t a) {
//   while(w--) {
//     uint8_t *osrc = src + (x >> 16);
//     uint8_t *col = &pal[*osrc << 2];
//     uint8_t r = col[0], g = col[1], b = col[2], a = col[3];

//     blend_rgba_rgba(dst, r, g, b, a);
//     dst += 4;
//     x += step;
//   }
// }