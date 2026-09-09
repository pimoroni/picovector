#pragma once

// How a pixel sits in the framebuffer, and the conversions to and from the
// premultiplied pixel_t every blend consumes. Storage is a separate concern from
// compositing (blend.hpp) and from authoring colour (color.hpp), so it lives in
// its own header: adding a format is adding a pack/expand pair here.
//
// Blending always happens at eight bits per channel. A blend in the packed 4-bit
// domain is not possible without widening anyway - the /15 needs a * 17, whose
// products overflow an 8-bit-spaced lane - so expanding to pixel_t is the
// arithmetic optimum rather than a compromise.
//
// At RGBA8888 everything here is the identity over the access the callers always
// used: pv_load and pv_store are a word dereference, and pv_px is a byte pointer.
// The RGBA8888 build compiles to the same instructions with this header as
// without it, and that is checked by disassembly, not assumed.

#include <cstdint>

#include "config.hpp"
#include "blend.hpp"
#include "util.hpp"

namespace picovector {

  // ── RGBA4444 ────────────────────────────────────────────────────────────────
  // R in bits 0-3, G in 4-7, B in 8-11, A in 12-15, so "R at the bottom" reads
  // the same as it does in pixel_t. Little-endian, byte 0 is [G:R] and byte 1 is
  // [A:B]; a consumer reading the buffer as bytes needs that.

  // Spread the four nibbles into the low nibble of their own byte in two doubling
  // steps, then scale by 17, which maps 0 to 0 and 15 to 255 exactly. Those two
  // values are what every early-out in blend.hpp tests for, so a stored opaque
  // pixel loads as opaque and a stored transparent one as the zero word.
  static inline __attribute__((always_inline))
  pixel_t pv_expand4444(uint16_t s) {
    uint32_t x = ((uint32_t)s | ((uint32_t)s << 8)) & 0x00ff00ffu;  // [A:B] in byte 2, [G:R] in byte 0
    uint32_t y = (x | (x << 4)) & 0x0f0f0f0fu;                     // one nibble per byte, R lowest
    return y * 17u;
  }

  // round(v / 17) in every byte lane at once, without a multiply, as
  // (v - v / 16 + 7 + [v mod 16 < 8]) / 16. The lane sum peaks at 247, so
  // nothing carries into a neighbour.
  //
  // Rounding rather than truncating. Truncation errs in [-15, 0], biasing every
  // store downwards into a visible darkening across a ramp; rounding errs in
  // [-8, +8] and makes pv_pack4444(pv_expand4444(x)) == x for all 65536 stored
  // words. That exactness is behavioural: pixelate_brush_t re-stores pixels it
  // has just read, and drifts if a round trip moves them.
  static inline __attribute__((always_inline))
  uint16_t pv_pack4444(pixel_t c) {
    uint32_t below_half = (~c >> 3) & 0x01010101u;
    uint32_t w = c - ((c >> 4) & 0x0f0f0f0fu) + 0x07070707u + below_half;
    uint32_t y = (w >> 4) & 0x0f0f0f0fu;          // one nibble per byte, R lowest
    uint32_t z = (y | (y >> 4)) & 0x00ff00ffu;    // [A:B] in byte 2, [G:R] in byte 0
    return (uint16_t)(z | (z >> 8));
  }

  // Both directions are monotonic per channel, which is what keeps a
  // premultiplied pixel valid across a round trip: channel <= alpha implies
  // pack(channel) <= pack(alpha) implies expand(pack(channel)) <=
  // expand(pack(alpha)). That is the carry-free precondition blend_over_premul
  // rests on, so no load needs to clamp against alpha.

  // ── the framebuffer's storage type ──────────────────────────────────────────
  // PV_PIXEL_FORMAT selects it for the whole build.

#if PV_PIXEL_FORMAT == PV_PIXEL_RGBA4444
  typedef uint16_t pv_store_t;
  static inline __attribute__((always_inline))
  pixel_t pv_load(const pv_store_t *p) { return pv_expand4444(*p); }
  static inline __attribute__((always_inline))
  void pv_store(pv_store_t *p, pixel_t c) { *p = pv_pack4444(c); }
  static inline __attribute__((always_inline))
  pv_store_t pv_pack(pixel_t c) { return pv_pack4444(c); }
  static inline __attribute__((always_inline))
  pixel_t pv_unpack(pv_store_t s) { return pv_expand4444(s); }
#else
  // Macros, not inline functions, so the preprocessed source at RGBA8888 is the
  // plain word access it always was and compiles to the same instructions.
  typedef uint32_t pv_store_t;
  #define pv_load(p) (*(p))
  #define pv_store(p, c) (*(p) = (c))
  #define pv_pack(c) (c)
  #define pv_unpack(s) (s)
#endif

  // A run of one colour. The stored form is invariant across the run, so it is
  // packed once and the loop is a plain store.
#if PV_PIXEL_FORMAT == PV_PIXEL_RGBA4444
  static inline __attribute__((always_inline))
  void pv_fill(pv_store_t *dst, pixel_t c, int n) {
    // Two stored pixels a word and two words a step, after a lone half-word
    // brings the pointer to word alignment.
    pv_store_t s = pv_pack4444(c);
    if(n > 0 && ((uintptr_t)dst & 2u)) { *dst++ = s; n--; }
    uint32_t pair = (uint32_t)s | ((uint32_t)s << 16);
    uint32_t *d = (uint32_t *)dst;
    int quads = n >> 2;
    if(quads) do { d[0] = pair; d[1] = pair; d += 2; } while(--quads);
    if(n & 2) *d++ = pair;
    if(n & 1) *(pv_store_t *)d = s;
  }
#else
  static inline __attribute__((always_inline))
  void pv_fill(pv_store_t *dst, pixel_t c, int n) {
    pv_store_t s = pv_pack(c);
    for(; n; n--) *dst++ = s;
  }
#endif

  // Compositing into storage. At RGBA8888 each of these is the callers' original
  // text, blend_over_premul on the loaded word followed by the store.
#if PV_PIXEL_FORMAT == PV_PIXEL_RGBA4444
  // Composite a premultiplied source over one stored pixel.
  static inline __attribute__((always_inline))
  void pv_blend_over(pv_store_t *dst, pixel_t src) {
    // The source alpha decides the outcome before the destination is read, so an
    // opaque or transparent source costs no expand and no blend.
    uint32_t a = src >> 24;
    if(a == 0u) return;
    if(a == 255u) { *dst = pv_pack4444(src); return; }
    *dst = pv_pack4444(blend_over_premul(pv_expand4444(*dst), src));
  }
  // Composite under coverage `m`. Zero coverage leaves the pixel untouched, which
  // matters because a span runs from the first to the last covered pixel of its
  // row, so a hollow shape leaves most of that run at zero.
  static inline __attribute__((always_inline))
  void pv_blend_masked(pv_store_t *dst, pixel_t src, uint32_t m) {
    if(m == 0u) return;
    pv_blend_over(dst, m == 255u ? src : _premul_mul_alpha(src, m));
  }
  // Composite a source already in stored form. An opaque source is copied as it
  // is, since a stored word survives a round trip unchanged.
  static inline __attribute__((always_inline))
  void pv_blend_stored(pv_store_t *dst, pv_store_t src) {
    uint32_t a = src >> 12;
    if(a == 0u) return;
    if(a == 15u) { *dst = src; return; }
    *dst = pv_pack4444(blend_over_premul(pv_expand4444(*dst), pv_expand4444(src)));
  }
#else
  #define pv_blend_over(dst, src) (*(dst) = blend_over_premul(*(dst), (src)))
  #define pv_blend_masked blend_masked_over_premul
  #define pv_blend_stored pv_blend_over
#endif

  // ── a stored pixel seen as channels ─────────────────────────────────────────
  // For the filter brushes, which read a pixel's channels and rewrite its colour
  // in place, leaving alpha. pv_r(p) to pv_a(p), or pv_ch(p, c), are the channels
  // as assignable 0..255 values, pv_word(p) is the whole pixel_t, and
  // p += PV_PX_STEP is the next pixel along the row.
  //
  // At RGBA8888 pv_px is the byte pointer those brushes always used and pv_r(p)
  // is p[0] itself, so the code they generate is unchanged. At RGBA4444 each is
  // a proxy over one nibble of the stored word.

#if PV_PIXEL_FORMAT == PV_PIXEL_RGBA4444
  typedef pv_store_t *pv_px;

  // round(v / 17) for every v in 0..255, the per-channel form of pv_pack4444. An
  // inline variable, so the build carries one copy.
  inline constexpr uint8_t pv_quant17[256] = {
     0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6,
     6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  7,
     7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
     8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
     8,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
     9,  9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15,
  };

  struct pv_nibble {
    pv_store_t *p; unsigned shift;
    inline operator int() const { return (int)(((*p >> shift) & 0xfu) * 17u); }
    // The low byte of v, as a byte store would keep
    inline pv_nibble &operator=(int v) {
      *p = (pv_store_t)((*p & ~(0xfu << shift)) | ((uint32_t)pv_quant17[(uint8_t)v] << shift));
      return *this;
    }
    inline pv_nibble &operator=(const pv_nibble &o) { return *this = (int)o; }
  };
  struct pv_wordref {
    pv_store_t *p;
    inline operator pixel_t() const { return pv_expand4444(*p); }
    inline pv_wordref &operator=(pixel_t c) { *p = pv_pack4444(c); return *this; }
  };

  static inline __attribute__((always_inline)) pv_nibble pv_r(pv_px p) { return { p,  0 }; }
  static inline __attribute__((always_inline)) pv_nibble pv_g(pv_px p) { return { p,  4 }; }
  static inline __attribute__((always_inline)) pv_nibble pv_b(pv_px p) { return { p,  8 }; }
  static inline __attribute__((always_inline)) pv_nibble pv_a(pv_px p) { return { p, 12 }; }
  static inline __attribute__((always_inline)) pv_nibble pv_ch(pv_px p, int c) { return { p, (unsigned)c * 4u }; }
  static inline __attribute__((always_inline)) pv_wordref pv_word(pv_px p) { return { p }; }
  #define PV_PX_STEP 1   // p += PV_PX_STEP is the next pixel along a row
  static inline __attribute__((always_inline)) int luminance(pv_px p) {
    return (int)((77u * pv_r(p) + 150u * pv_g(p) + 29u * pv_b(p)) >> 8);
  }
#else
  // Macros, not inline functions: with these the preprocessed brush source is the
  // byte-indexed original, and the compiler sees nothing to schedule differently.
  typedef uint8_t *pv_px;
  #define pv_r(p) ((p)[0])
  #define pv_g(p) ((p)[1])
  #define pv_b(p) ((p)[2])
  #define pv_a(p) ((p)[3])
  #define pv_ch(p, c) ((p)[(c)])
  #define pv_word(p) (*(uint32_t *)(p))
  #define PV_PX_STEP 4
#endif

}
