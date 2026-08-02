// The colour type: authored components, and arithmetic over them.
//
// A colour keeps the three components it was authored with and the space they
// belong to, so `color.oklch(70, 40, 250).l` is 70 rather than whatever falls
// out of the resolved pixel. Everything here also holds after slicing to
// color_t, because that is the only form the bindings ever hand back.

#include <array>
#include <cmath>
#include <cstdlib>

#include "test.hpp"
#include "picovector.hpp"
#include "color.hpp"
#include "blend.hpp"

using namespace picovector;

// The premultiplied word has to stay in step with the resolved sRGB, whatever
// produced the colour. Stated independently of premultiply() itself.
static bool premul_agrees(const color_t &c) {
  return _r(c._p) == (uint32_t)(c.r() * c.a() / 255)
      && _g(c._p) == (uint32_t)(c.g() * c.a() / 255)
      && _b(c._p) == (uint32_t)(c.b() * c.a() / 255)
      && _a(c._p) == (uint32_t)c.a();
}

static int brightness(const color_t &c) { return c.r() + c.g() + c.b(); }

void test_color() {
  printf("colour: authored components survive construction and slicing\n");
  {
    color_t rgb = rgb_color_t(200, 100, 50, 255);
    CHECK(rgb.space() == COLOR_RGB);
    CHECK(rgb.r() == 200 && rgb.g() == 100 && rgb.b() == 50 && rgb.a() == 255);

    color_t hsv = hsv_color_t(100, 200, 128, 200);
    CHECK(hsv.space() == COLOR_HSV);
    CHECK(hsv.h() == 100 && hsv.s() == 200 && hsv.v() == 128 && hsv.a() == 200);

    color_t oklch = oklch_color_t(70, 40, 250, 255);
    CHECK(oklch.space() == COLOR_OKLCH);
    CHECK(oklch.l() == 70 && oklch.c() == 40 && oklch.h() == 250);
    CHECK(oklch.a() == 255);
  }

  printf("colour: a resolved colour agrees with its premultiplied word\n");
  {
    for(int a : {255, 200, 128, 1, 0}) {
      CHECK(premul_agrees(rgb_color_t(200, 100, 50, (uint8_t)a)));
      CHECK(premul_agrees(hsv_color_t(100, 200, 128, (uint8_t)a)));
      CHECK(premul_agrees(oklch_color_t(70, 40, 250, (uint8_t)a)));
    }
  }

  printf("colour: every arithmetic result is self-consistent too\n");
  {
    const color_t sources[3] = {
      rgb_color_t(200, 100, 50, 200),
      hsv_color_t(100, 200, 128, 200),
      oklch_color_t(70, 40, 250, 200),
    };
    for(const color_t &c : sources) {
      CHECK(premul_agrees(c.lighten(30)));
      CHECK(premul_agrees(c.darken(30)));
      CHECK(premul_agrees(c.scale(150)));
      CHECK(premul_agrees(c.with_alpha(64)));
      CHECK(premul_agrees(c.with_component(1, 12)));
      for(int t : {0, 1, 128, 254, 255}) {
        CHECK(premul_agrees(c.mix(rgb_color_t(0, 128, 255, 255), (uint8_t)t)));
        CHECK(premul_agrees(c.mix(sources[2], (uint8_t)t)));
      }
    }
  }

  printf("colour: lighten and darken act on the lightness component only\n");
  {
    // OKLCH: l moves, chroma and hue are left exactly as authored.
    color_t o = oklch_color_t(70, 40, 250, 255);
    color_t up = o.lighten(10);
    CHECK(up.l() == 80 && up.c() == 40 && up.h() == 250);
    CHECK(up.space() == COLOR_OKLCH);
    CHECK(brightness(up) > brightness(o));   // and the resolved sRGB followed

    // HSV: v moves, hue and saturation are left alone.
    color_t h = hsv_color_t(100, 200, 128, 255);
    color_t hup = h.lighten(10);
    CHECK(hup.h() == 100 && hup.s() == 200 && hup.v() == 138);
    CHECK(brightness(hup) > brightness(h));

    // RGB has no lightness component, so all three channels move together.
    color_t c = rgb_color_t(200, 100, 50, 255);
    color_t cup = c.lighten(10);
    CHECK(cup.r() == 210 && cup.g() == 110 && cup.b() == 60);
    CHECK(c.darken(10) == rgb_color_t(190, 90, 40, 255));
  }

  printf("colour: arithmetic identities and clamping\n");
  {
    const color_t sources[3] = {
      rgb_color_t(200, 100, 50, 200),
      hsv_color_t(100, 200, 128, 200),
      oklch_color_t(70, 40, 250, 200),
    };
    for(const color_t &c : sources) {
      CHECK(c.lighten(0) == c);
      CHECK(c.darken(0) == c);
      CHECK(c.scale(100) == c);
      CHECK(c.with_alpha(c.a()) == c);

      // alpha replacement leaves the colour itself alone
      color_t faded = c.with_alpha(64);
      CHECK(faded.a() == 64);
      CHECK(faded.r() == c.r() && faded.g() == c.g() && faded.b() == c.b());
      CHECK(faded.space() == c.space());

      // a big step in either direction saturates rather than wrapping
      CHECK(brightness(c.darken(400)) <= brightness(c));
      CHECK(brightness(c.lighten(400)) >= brightness(c));
      CHECK(premul_agrees(c.scale(10000)));
    }

    CHECK(rgb_color_t(250, 250, 250, 255).lighten(20) == rgb_color_t(255, 255, 255, 255));
    CHECK(rgb_color_t(5, 5, 5, 255).darken(20) == rgb_color_t(0, 0, 0, 255));
    CHECK(rgb_color_t(200, 100, 50, 255).scale(50) == rgb_color_t(100, 50, 25, 255));

    // and on the one component that means lightness, in each space that has one
    CHECK(oklch_color_t(200, 40, 250, 255).lighten(100).l() == 255);
    CHECK(oklch_color_t(20, 40, 250, 255).darken(100).l() == 0);
    CHECK(oklch_color_t(70, 40, 250, 255).scale(1000).l() == 255);
    CHECK(hsv_color_t(100, 200, 200, 255).lighten(100).v() == 255);
    CHECK(hsv_color_t(100, 200, 20, 255).darken(100).v() == 0);
    CHECK(hsv_color_t(100, 200, 200, 255).scale(1000).v() == 255);
  }

  printf("colour: mix hits both endpoints exactly\n");
  {
    color_t a = oklch_color_t(70, 40, 250, 255);
    color_t b = oklch_color_t(30, 90, 10, 128);
    CHECK(a.mix(b, 0) == a);
    CHECK(a.mix(b, 255) == b);
    CHECK(a.mix(b, 255).a() == b.a());

    // across spaces too, where the result is an RGB colour
    color_t c = rgb_color_t(0, 128, 255, 255);
    CHECK(a.mix(c, 0) == a);
    CHECK(a.mix(c, 255) == c);
  }

  printf("colour: mix takes the short way round the hue wheel\n");
  {
    color_t a = hsv_color_t(250, 255, 255, 255);
    color_t b = hsv_color_t(10, 255, 255, 255);
    color_t half = a.mix(b, 128);
    CHECK(half.space() == COLOR_HSV);
    CHECK(half.h() == 2);                          // 250 -> 10 the short way, via 0
    CHECK(half.h() < 20 || half.h() > 240);        // i.e. not through the spectrum
    CHECK(a.mix(b, 0).h() == 250 && a.mix(b, 255).h() == 10);

    // the same holds for an OKLCH hue, which lives in a different slot
    color_t oa = oklch_color_t(70, 40, 250, 255);
    color_t ob = oklch_color_t(70, 40, 10, 255);
    CHECK(oa.mix(ob, 128).h() == 2);
    CHECK(oa.mix(ob, 128).l() == 70 && oa.mix(ob, 128).c() == 40);
  }

  printf("colour: mixing two spaces interpolates the resolved sRGB\n");
  {
    color_t a = oklch_color_t(70, 40, 250, 255);
    color_t b = rgb_color_t(0, 128, 255, 255);
    color_t half = a.mix(b, 128);
    CHECK(half.space() == COLOR_RGB);

    // within a count of the ideal lerp, whatever the integer rounding does
    float t = 128.0f / 255.0f;
    CHECK(std::abs((int)half.r() - (int)lroundf(a.r() + (b.r() - a.r()) * t)) <= 1);
    CHECK(std::abs((int)half.g() - (int)lroundf(a.g() + (b.g() - a.g()) * t)) <= 1);
    CHECK(std::abs((int)half.b() - (int)lroundf(a.b() + (b.b() - a.b()) * t)) <= 1);
  }

  printf("colour: over() composites a colour onto a background\n");
  {
    color_t bg = rgb_color_t(0, 0, 0, 255);
    color_t fg = rgb_color_t(200, 100, 50, 255);

    CHECK(fg.over(bg) == fg);                          // opaque covers entirely
    CHECK(fg.with_alpha(0).over(bg) == bg);             // transparent changes nothing
    CHECK(fg.over(bg).space() == COLOR_RGB);

    // half alpha over black lands about half way, and the result is opaque
    color_t half = fg.with_alpha(128).over(bg);
    CHECK(half.a() == 255);
    CHECK(std::abs((int)half.r() - 100) <= 2);
    CHECK(std::abs((int)half.g() - 50) <= 2);
    CHECK(std::abs((int)half.b() - 25) <= 2);
    CHECK(premul_agrees(half));

    // over a white background it lands the other side of the same midpoint
    color_t onwhite = fg.with_alpha(128).over(rgb_color_t(255, 255, 255, 255));
    CHECK(onwhite.r() > half.r() && onwhite.g() > half.g() && onwhite.b() > half.b());

    // an OKLCH foreground still composites, and reports RGB afterwards
    color_t o = oklch_color_t(70, 40, 250, 128);
    CHECK(o.over(bg).space() == COLOR_RGB);
    CHECK(premul_agrees(o.over(bg)));
  }

  printf("colour: with_component replaces one authored component\n");
  {
    color_t o = oklch_color_t(70, 40, 250, 200);
    color_t lighter = o.with_component(0, 200);
    CHECK(lighter.l() == 200 && lighter.c() == 40 && lighter.h() == 250);
    CHECK(lighter.a() == 200 && lighter.space() == COLOR_OKLCH);
    CHECK(brightness(lighter) > brightness(o));

    // out-of-range indices are ignored rather than writing past the components
    CHECK(o.with_component(-1, 0) == o);
    CHECK(o.with_component(3, 0) == o);

    // and out-of-range values clamp rather than wrapping, since the bindings
    // hand these straight through from Python
    CHECK(o.with_component(0, 400).l() == 255);
    CHECK(o.with_component(0, -5).l() == 0);
    CHECK(o.with_alpha(400).a() == 255);
    CHECK(o.mix(rgb_color_t(0, 0, 0, 255), 400) == o.mix(rgb_color_t(0, 0, 0, 255), 255));
  }

  printf("colour: equality compares the rendered colour, not the authoring space\n");
  {
    color_t o = oklch_color_t(70, 40, 250, 255);
    color_t same = rgb_color_t(o.r(), o.g(), o.b(), 255);
    CHECK(o == same);
    CHECK(o.space() != same.space());
    CHECK(rgb_color_t(1, 2, 3, 255) != rgb_color_t(1, 2, 4, 255));
  }

  printf("colour: the layout fits the GC block budget\n");
  {
    // Also static_asserted in color.hpp and brush.hpp; restated here because
    // this is where someone looks after changing the type.
    CHECK(sizeof(color_t) == 12);
    CHECK(sizeof(rgb_color_t) == sizeof(color_t));
    CHECK(sizeof(hsv_color_t) == sizeof(color_t));
    CHECK(sizeof(oklch_color_t) == sizeof(color_t));
  }
}
