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

  // Two SWAR lanes 16 bits apart, the same shape _premul_mul_alpha uses: spread
  // each nibble into its own byte, then scale by 17, which maps 0 to 0 and 15 to
  // 255 exactly. Those two values are what every early-out in blend.hpp tests
  // for, so a stored opaque pixel loads as opaque and a stored transparent one as
  // the zero word.
  //
  // The * 0x101 spread is x + (x << 8). The two nibbles collide in bits 8..11 and
  // can sum to 30, but that carries only as far as bit 12, which the mask drops.
  static inline __attribute__((always_inline))
  pixel_t pv_expand4444(uint16_t s) {
    uint32_t rb = ((uint32_t)( s       & 0x0f0fu) * 0x101u) & 0x000f000fu;  // R:[0:3], B:[16:19]
    uint32_t ga = ((uint32_t)((s >> 4) & 0x0f0fu) * 0x101u) & 0x000f000fu;  // G:[0:3], A:[16:19]
    return (rb * 17u) | ((ga * 17u) << 8);
  }

  // (v * 241 + 2048) >> 12 is exactly round(v / 17) for every v in 0..255, and
  // the product peaks at 63503, so two channels ride one multiply.
  //
  // Rounding rather than truncating. Truncation errs in [-15, 0], biasing every
  // store downwards into a visible darkening across a ramp; rounding errs in
  // [-8, +8] and makes pv_pack4444(pv_expand4444(x)) == x for all 65536 stored
  // words. That exactness is behavioural: pixelate_brush_t re-stores pixels it
  // has just read, and drifts if a round trip moves them.
  static inline __attribute__((always_inline))
  uint16_t pv_pack4444(pixel_t c) {
    uint32_t rb = ((( c       & 0x00ff00ffu) * 241u + 0x08000800u) >> 12) & 0x000f000fu;
    uint32_t ga = ((((c >> 8) & 0x00ff00ffu) * 241u + 0x08000800u) >> 12) & 0x000f000fu;
    uint32_t rbp = (rb | (rb >> 8)) & 0x0f0fu;   // R:[0:3], B:[8:11]
    uint32_t gap = (ga | (ga >> 8)) & 0x0f0fu;   // G:[0:3], A:[8:11]
    return (uint16_t)(rbp | (gap << 4));
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
  static inline __attribute__((always_inline))
  void pv_fill(pv_store_t *dst, pixel_t c, int n) {
    pv_store_t s = pv_pack(c);
    for(; n; n--) *dst++ = s;
  }

  // Composite a premultiplied source into one stored pixel under coverage `m`.
  // The storage-aware form of blend.hpp's blend_masked_over_premul: zero coverage
  // leaves the pixel untouched, which matters because the rasteriser emits one
  // span per row from the first to the last covered pixel, so a hollow shape
  // leaves most of that run at zero.
#if PV_PIXEL_FORMAT == PV_PIXEL_RGBA4444
  static inline __attribute__((always_inline))
  void pv_blend_masked(pv_store_t *dst, pixel_t src, uint32_t m) {
    if(m == 0u) return;
    pv_store(dst, blend_over_premul(pv_load(dst), m == 255u ? src : _premul_mul_alpha(src, m)));
  }
#else
  #define pv_blend_masked blend_masked_over_premul
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

  struct pv_nibble {
    pv_store_t *p; unsigned shift;
    inline operator uint8_t() const { return (uint8_t)(((*p >> shift) & 0xfu) * 17u); }
    // round(v / 17), the per-channel form of pv_pack4444
    inline pv_nibble &operator=(int v) {
      *p = (pv_store_t)((*p & ~(0xfu << shift)) | ((((uint32_t)v * 241u + 2048u) >> 12) << shift));
      return *this;
    }
    inline pv_nibble &operator=(const pv_nibble &o) { return *this = (int)(uint8_t)o; }
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
