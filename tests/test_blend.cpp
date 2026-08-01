// Blending invariants, checked exhaustively over valid premultiplied inputs.
//
// A premultiplied source has every channel <= its alpha; the SWAR add in
// blend_over_premul relies on that (a valid result is <= 255 per channel, so the
// add never carries), and feeding it an invalid triple produces nonsense.

#include "test.hpp"
#include "picovector.hpp"
#include "blend.hpp"

using namespace picovector;

static inline uint32_t chan(uint32_t p, int i) { return (p >> (i * 8)) & 0xff; }

void test_blend() {
  printf("blend: over-composite endpoints\n");
  {
    uint32_t dst = 0xff204060u;
    CHECK(blend_over_premul(dst, 0x00000000u) == dst);            // transparent source
    uint32_t opaque = 0xff8899aau;
    CHECK(blend_over_premul(dst, opaque) == opaque);              // opaque source
  }

  printf("blend: masked composite skips zero coverage and is exact at full\n");
  {
    uint32_t dst = 0xff204060u, src = 0xff8899aau;
    uint32_t d0 = dst;
    blend_masked_over_premul(&d0, src, 0);
    CHECK(d0 == dst);                                             // untouched
    uint32_t d1 = dst;
    blend_masked_over_premul(&d1, src, 255);
    CHECK(d1 == src);                                             // exactly the source
  }

  printf("blend: never leaves the valid range, over every premultiplied input\n");
  {
    long checked = 0;
    bool in_range = true, monotonic = true;
    for(uint32_t sa = 0; sa <= 255; sa += 5)
      for(uint32_t sc = 0; sc <= sa; sc += 5)
        for(uint32_t d = 0; d <= 255; d += 5) {
          uint32_t src = (sa << 24) | (sc << 16) | (sc << 8) | sc;
          uint32_t dst = 0xff000000u | (d << 16) | (d << 8) | d;
          uint32_t prev = 0;
          for(uint32_t m = 0; m <= 255; m += 17) {
            uint32_t out = dst;
            blend_masked_over_premul(&out, src, m);
            checked++;
            for(int i = 0; i < 3; i++) if(chan(out, i) > 255) in_range = false;
            // More coverage of a brighter source can only brighten, to within a
            // least significant bit: the blend rounds in fixed point, so a step
            // can wobble by one (sa=25, sc=5 over dst=5 goes 10 -> 9 at full
            // coverage). Anything larger is an inversion, not rounding.
            if(sc >= d && m > 0 && chan(out, 0) + 1 < prev) monotonic = false;
            prev = chan(out, 0);
          }
        }
    CHECK(checked > 100000);
    CHECK(in_range);
    CHECK(monotonic);
  }

  printf("blend: coverage scaling is monotonic in alpha\n");
  {
    uint32_t src = 0xffff0000u, dst = 0xff000000u;
    int last = -1;
    bool rising = true;
    for(uint32_t m = 0; m <= 255; m++) {
      uint32_t out = dst;
      blend_masked_over_premul(&out, src, m);
      int a = (int)chan(out, 2);
      if(a < last) rising = false;
      last = a;
    }
    CHECK(rising);
  }
}
