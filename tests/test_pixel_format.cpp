// The pixel storage conversions.
//
// Everything here is arithmetic, so most of it is checked exhaustively rather
// than sampled. Two properties are behavioural rather than cosmetic: a round trip
// through storage must leave an already-stored word alone, because the effect
// brushes re-store pixels they have just read; and it must keep a premultiplied
// pixel valid (every channel <= its alpha), because that is the carry-free
// precondition blend_over_premul rests on.

#include "test.hpp"
#include "picovector.hpp"
#include "blend.hpp"
#include "pixel_store.hpp"

using namespace picovector;

// round(v / 17), what packing one channel is meant to produce
static inline uint32_t ref_quantise(uint32_t v) { return (2u * v + 17u) / 34u; }

static inline pixel_t pack_channels(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
  return r | (g << 8) | (b << 16) | (a << 24);
}

void test_pixel_format() {
  printf("pixel: 4444 endpoints are exact\n");
  {
    CHECK(pv_expand4444(0x0000u) == 0x00000000u);
    CHECK(pv_expand4444(0xffffu) == 0xffffffffu);
    CHECK(pv_pack4444(0x00000000u) == 0x0000u);
    CHECK(pv_pack4444(0xffffffffu) == 0xffffu);
  }

  printf("pixel: nibbles land in the right channels\n");
  {
    uint16_t stored = (uint16_t)((10u << 12) | (2u << 8) | (1u << 4) | 3u);  // A=10 B=2 G=1 R=3
    pixel_t p = pv_expand4444(stored);
    CHECK(_r(p) == 3u * 17u);
    CHECK(_g(p) == 1u * 17u);
    CHECK(_b(p) == 2u * 17u);
    CHECK(_a(p) == 10u * 17u);
    CHECK(pv_pack4444(p) == stored);
  }

  printf("pixel: a stored word survives a round trip untouched, all 65536\n");
  {
    bool stable = true;
    for(uint32_t s = 0; s <= 0xffffu; s++)
      if(pv_pack4444(pv_expand4444((uint16_t)s)) != (uint16_t)s) { stable = false; break; }
    CHECK(stable);
  }

  printf("pixel: packing a channel is round(v / 17)\n");
  {
    bool rounded = true;
    for(uint32_t v = 0; v <= 255 && rounded; v++)
      for(int i = 0; i < 4; i++) {
        uint32_t stored = pv_pack4444(v << (i * 8));
        if(((stored >> (i * 4)) & 0xfu) != ref_quantise(v)) { rounded = false; break; }
      }
    CHECK(rounded);
  }

  printf("pixel: a channel never moves by more than half a step\n");
  {
    int worst = 0;
    for(uint32_t v = 0; v <= 255; v++) {
      int back = (int)_r(pv_expand4444(pv_pack4444(v)));
      int err = back > (int)v ? back - (int)v : (int)v - back;
      if(err > worst) worst = err;
    }
    CHECK_MSG(worst <= 8, "worst-case channel error");
  }

  printf("pixel: both directions are monotonic per channel\n");
  {
    bool monotonic = true;
    for(uint32_t v = 1; v <= 255; v++)
      if((pv_pack4444(v) & 0xfu) < (pv_pack4444(v - 1) & 0xfu)) { monotonic = false; break; }
    for(uint32_t n = 1; n <= 15; n++)
      if(_r(pv_expand4444((uint16_t)n)) < _r(pv_expand4444((uint16_t)(n - 1)))) { monotonic = false; break; }
    CHECK(monotonic);
  }

  printf("pixel: a round trip keeps a premultiplied pixel valid\n");
  {
    // monotonicity implies this, but it is the property blend_over_premul needs,
    // so assert it over every valid channel/alpha pair rather than inferring it
    bool valid = true;
    long checked = 0;
    for(uint32_t a = 0; a <= 255 && valid; a++)
      for(uint32_t c = 0; c <= a; c++) {
        pixel_t back = pv_expand4444(pv_pack4444(pack_channels(c, c, c, a)));
        checked++;
        if(_r(back) > _a(back) || _g(back) > _a(back) || _b(back) > _a(back)) { valid = false; break; }
      }
    CHECK(valid);
    CHECK(checked == 32896);
  }

  printf("pixel: an opaque store still loads as opaque\n");
  {
    // blend_over_premul short-circuits on a == 255 and a == 0; both have to survive
    bool opaque_kept = true;
    for(uint32_t c = 0; c <= 255; c += 17)
      if(_a(pv_expand4444(pv_pack4444(pack_channels(c, c, c, 255)))) != 255u) { opaque_kept = false; break; }
    CHECK(opaque_kept);
    CHECK(pv_expand4444(pv_pack4444(0x00000000u)) == 0x00000000u);
  }

  printf("pixel: compositing into storage settles instead of drifting\n");
  {
    // every store quantises, so a repeated composite has to reach a fixed point
    bool settles = true;
    for(uint32_t sa = 17; sa <= 255 && settles; sa += 34)
      for(uint32_t sc = 0; sc <= sa; sc += 51) {
        pixel_t src = pack_channels(sc, sc, sc, sa);
        uint16_t d = pv_pack4444(0xff000000u);
        int steps = 0;
        for(; steps < 64; steps++) {
          uint16_t next = pv_pack4444(blend_over_premul(pv_expand4444(d), src));
          if(next == d) break;
          d = next;
        }
        if(steps >= 64) { settles = false; break; }
      }
    CHECK(settles);
  }

  printf("pixel: the store layer round-trips a pixel through a cell\n");
  {
#if PV_PIXEL_FORMAT == PV_PIXEL_RGBA8888
    CHECK(sizeof(pv_store_t) == 4);
    pv_store_t cell = 0;
    pv_store(&cell, 0x8040201fu);
    CHECK(cell == 0x8040201fu);          // the identity at eight bits
    CHECK(pv_load(&cell) == 0x8040201fu);
#else
    CHECK(sizeof(pv_store_t) == 2);
    pv_store_t cell = 0;
    pv_store(&cell, 0xffffffffu);
    CHECK(cell == 0xffffu);
    CHECK(pv_load(&cell) == 0xffffffffu);
#endif
  }
}
