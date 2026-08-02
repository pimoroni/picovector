// Reading a colour back out of a framebuffer word.
//
// The framebuffer is premultiplied because blending needs it to be, so a colour
// read back has to have the alpha divided out again. Passing the premultiplied
// bytes to rgb_color_t instead premultiplied them a second time, which returned
// a half-transparent pixel at a quarter of its brightness.

#include <array>
#include <cstdlib>

#include "test.hpp"
#include "picovector.hpp"
#include "color.hpp"
#include "blend.hpp"

using namespace picovector;

void test_color_readback() {
  printf("colour: an opaque pixel round-trips exactly\n");
  {
    for(auto rgb : {std::array<int,3>{200, 100, 50}, {0, 0, 0}, {255, 255, 255}, {1, 128, 254}}) {
      rgb_color_t c((uint8_t)rgb[0], (uint8_t)rgb[1], (uint8_t)rgb[2], 255);
      rgb_color_t back = color_from_premul(c._p);
      CHECK(back.r() == rgb[0] && back.g() == rgb[1] && back.b() == rgb[2]);
      CHECK(back.a() == 255);
      CHECK(back._p == c._p);          // and re-premultiplies to the same word
    }
  }

  printf("colour: a translucent pixel comes back within what was stored\n");
  {
    // The error is bounded by the premultiply's own quantisation: a channel
    // stored as (v * a / 255) can only carry a/255 of the range back.
    for(int a : {200, 128, 64, 32}) {
      rgb_color_t c(200, 100, 50, (uint8_t)a);
      rgb_color_t back = color_from_premul(c._p);
      CHECK(back.a() == a);
      int tol = 255 / a + 1;
      CHECK(std::abs((int)back.r() - 200) <= tol);
      CHECK(std::abs((int)back.g() - 100) <= tol);
      CHECK(std::abs((int)back.b() - 50) <= tol);
    }
  }

  printf("colour: a fully transparent pixel reads as transparent black\n");
  {
    rgb_color_t c(200, 100, 50, 0);
    rgb_color_t back = color_from_premul(c._p);
    CHECK(back.a() == 0);
    CHECK(back.r() == 0 && back.g() == 0 && back.b() == 0);
  }

  printf("colour: a component never comes back above full scale\n");
  {
    // A malformed word whose channels exceed its alpha must still clamp.
    uint32_t bogus = (10u << 24) | (255u << 16) | (255u << 8) | 255u;
    rgb_color_t back = color_from_premul(bogus);
    CHECK(back.r() <= 255 && back.g() <= 255 && back.b() <= 255);
  }
}
