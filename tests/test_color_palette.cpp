// Reading a colour back in OKLCH, measuring one against another, and the gamut
// work that everything generative depends on.
//
// Reference values are computed from Bjorn Ottosson's published matrices in
// Python, independently of the implementation, and quoted in the comments.

#include <cmath>
#include <cstdlib>

#include "test.hpp"
#include "picovector.hpp"
#include "brush.hpp"
#include "color.hpp"

using namespace picovector;

namespace {

  // Hue is a wheel: 250 and 6 are twelve counts apart, not 244.
  int hue_apart(int a, int b) {
    int d = std::abs(a - b) & 0xff;
    return d > 128 ? 256 - d : d;
  }

}

void test_color_palette() {
  printf("palette: an sRGB colour reads back in OKLCH\n");
  {
    // rgb(255, 0, 0) is L 0.6280, C 0.2577, hue 29.23 degrees, which in bytes is
    // l 160, c 188, h 21.
    color_t red = rgb_color_t(255, 0, 0, 255).to_oklch();
    CHECK(red.space() == COLOR_OKLCH);
    CHECK_MSG(std::abs((int)red.l() - 160) <= 1, "red lightness");
    CHECK_MSG(std::abs((int)red.c() - 188) <= 1, "red chroma");
    CHECK_MSG(hue_apart(red.h(), 21) <= 1, "red hue");

    // rgb(0, 255, 0): L 0.8664, C 0.2948, 142.50 degrees -> 221, 215, 101
    color_t green = rgb_color_t(0, 255, 0, 255).to_oklch();
    CHECK_MSG(std::abs((int)green.l() - 221) <= 1, "green lightness");
    CHECK_MSG(std::abs((int)green.c() - 215) <= 1, "green chroma");
    CHECK_MSG(hue_apart(green.h(), 101) <= 1, "green hue");

    // rgb(0, 0, 255): L 0.4520, C 0.3132, 264.05 degrees -> 115, 228, 188
    color_t blue = rgb_color_t(0, 0, 255, 255).to_oklch();
    CHECK_MSG(std::abs((int)blue.l() - 115) <= 1, "blue lightness");
    CHECK_MSG(std::abs((int)blue.c() - 228) <= 1, "blue chroma");
    CHECK_MSG(hue_apart(blue.h(), 188) <= 1, "blue hue");
  }

  printf("palette: the achromatic colours land where they should\n");
  {
    color_t white = rgb_color_t(255, 255, 255, 255).to_oklch();
    CHECK(white.l() == 255);
    CHECK(white.c() == 0);

    color_t black = rgb_color_t(0, 0, 0, 255).to_oklch();
    CHECK(black.l() == 0);
    CHECK(black.c() == 0);

    // Mid grey is L 0.5999 - OKLab lightness is perceptual, so half-way up the
    // byte scale is nowhere near half-way up the channel scale.
    color_t grey = rgb_color_t(128, 128, 128, 255).to_oklch();
    CHECK_MSG(std::abs((int)grey.l() - 153) <= 1, "mid grey lightness");
    CHECK(grey.c() == 0);
  }

  printf("palette: converting to the space it is already in changes nothing\n");
  {
    color_t ok = oklch_color_t(70, 40, 250, 200);
    color_t same = ok.to_oklch();
    CHECK(same.l() == 70 && same.c() == 40 && same.h() == 250);
    CHECK(same.a() == 200);
    CHECK(same == ok);

    color_t rgb = rgb_color_t(200, 100, 50, 128);
    color_t still = rgb.to_rgb();
    CHECK(still.r() == 200 && still.g() == 100 && still.b() == 50 && still.a() == 128);
    CHECK(still.space() == COLOR_RGB);
  }

  printf("palette: to_rgb resolves whatever space it came from\n");
  {
    color_t ok = oklch_color_t(160, 100, 21, 255);
    color_t as_rgb = ok.to_rgb();
    CHECK(as_rgb.space() == COLOR_RGB);
    CHECK(as_rgb.r() == ok.r() && as_rgb.g() == ok.g() && as_rgb.b() == ok.b());
    CHECK(as_rgb == ok);   // same pixel, different authoring
  }

  printf("palette: an in-gamut colour round-trips as the same colour\n");
  {
    // The invariant that matters is the colour, not the digits. Components are
    // bytes at both ends, and a dark, faint colour resolves to only a handful of
    // channel levels - rgb(20, 10, 5) cannot pin a hue to better than a few
    // counts, and no implementation could. So this asks that the trip does not
    // move the pixel, and does not move the colour perceptibly.
    bool held = true;
    float worst = 0.0f;
    for(int l = 20; l <= 240; l += 10) {
      for(int h = 0; h < 256; h += 8) {
        uint8_t c = color_t::max_chroma((uint8_t)l, (uint8_t)h) / 2;
        color_t start = oklch_color_t((uint8_t)l, c, (uint8_t)h, 255);
        color_t back = start.to_rgb().to_oklch();

        if(std::abs((int)back.r() - (int)start.r()) > 1) held = false;
        if(std::abs((int)back.g() - (int)start.g()) > 1) held = false;
        if(std::abs((int)back.b() - (int)start.b()) > 1) held = false;

        float d = start.difference(back);
        if(d > worst) worst = d;
      }
    }
    CHECK_MSG(held, "a round trip moved the pixel");
    // Comfortably inside the ~2 where a difference starts to be noticeable.
    CHECK_MSG(worst < 1.0f, "a round trip moved the colour");
  }

  printf("palette: components round-trip closely where there is room for them\n");
  {
    // Away from the ends, with real chroma behind the hue, the digits survive
    // too - which is what makes reading .l/.c/.h off a colour worth doing.
    bool held = true;
    for(int l = 80; l <= 200; l += 10) {
      for(int h = 0; h < 256; h += 8) {
        uint8_t c = color_t::max_chroma((uint8_t)l, (uint8_t)h) / 2;
        if(c < 24) continue;
        color_t start = oklch_color_t((uint8_t)l, c, (uint8_t)h, 255);
        color_t back = start.to_rgb().to_oklch();

        if(std::abs((int)back.l() - l) > 2) held = false;
        if(std::abs((int)back.c() - (int)c) > 2) held = false;
        if(hue_apart(back.h(), h) > 2) held = false;
      }
    }
    CHECK_MSG(held, "an OKLCH colour did not survive the round trip");
  }

  printf("palette: alpha survives a change of space\n");
  {
    color_t translucent = rgb_color_t(200, 100, 50, 77);
    CHECK(translucent.to_oklch().a() == 77);
    CHECK(translucent.to_oklch().to_rgb().a() == 77);
  }

  printf("palette: luminance is the light, not the lightness\n");
  {
    CHECK(rgb_color_t(0, 0, 0, 255).luminance() == 0.0f);
    CHECK(std::abs(rgb_color_t(255, 255, 255, 255).luminance() - 1.0f) < 0.001f);

    // Weighted heavily toward green, which is why a yellow reads so much
    // brighter than a blue at the same OKLCH lightness.
    color_t g = rgb_color_t(0, 255, 0, 255);
    color_t b = rgb_color_t(0, 0, 255, 255);
    CHECK(std::abs(g.luminance() - 0.7152f) < 0.001f);
    CHECK(std::abs(b.luminance() - 0.0722f) < 0.001f);
  }

  printf("palette: contrast matches the published ratios\n");
  {
    color_t white = rgb_color_t(255, 255, 255, 255);
    color_t black = rgb_color_t(0, 0, 0, 255);

    CHECK(std::abs(black.contrast(white) - 21.0f) < 0.01f);
    CHECK(std::abs(white.contrast(black) - 21.0f) < 0.01f);   // order does not matter
    CHECK(std::abs(white.contrast(white) - 1.0f) < 0.001f);

    // The canonical worked example: #767676 is the darkest grey that still
    // clears 4.5 against white.
    color_t grey = rgb_color_t(118, 118, 118, 255);
    CHECK_MSG(std::abs(grey.contrast(white) - 4.54f) < 0.01f, "#767676 on white");
    CHECK(rgb_color_t(119, 119, 119, 255).contrast(white) < 4.5f);
  }

  printf("palette: difference is zero for a colour against itself\n");
  {
    color_t c = rgb_color_t(200, 100, 50, 255);
    CHECK(c.difference(c) == 0.0f);

    // Black to white is the full scale.
    color_t white = rgb_color_t(255, 255, 255, 255);
    color_t black = rgb_color_t(0, 0, 0, 255);
    CHECK_MSG(std::abs(black.difference(white) - 100.0f) < 0.01f, "black to white is 100");
    CHECK(std::abs(white.difference(black) - black.difference(white)) < 0.001f);
  }

  printf("palette: difference grows with separation\n");
  {
    color_t base = rgb_color_t(128, 128, 128, 255);
    float last = -1.0f;
    bool rising = true;
    for(int d = 0; d <= 100; d += 10) {
      float now = base.difference(rgb_color_t((uint8_t)(128 + d), 128, 128, 255));
      if(now < last) rising = false;
      last = now;
    }
    CHECK(rising);

    // A single count of a channel is well under the threshold where anyone
    // notices; a tenth of the range is well over it.
    CHECK(base.difference(rgb_color_t(129, 128, 128, 255)) < 2.0f);
    CHECK(base.difference(rgb_color_t(153, 128, 128, 255)) > 2.0f);
  }

  printf("palette: alpha does not enter into either measurement\n");
  {
    color_t opaque = rgb_color_t(200, 100, 50, 255);
    color_t ghost = rgb_color_t(200, 100, 50, 8);
    CHECK(opaque.luminance() == ghost.luminance());
    CHECK(opaque.difference(ghost) == 0.0f);
    CHECK(opaque.contrast(ghost) == 1.0f);
  }

  printf("palette: a colour with more chroma than sRGB has is out of gamut\n");
  {
    // Full chroma is outside sRGB at every hue, at any lightness worth having.
    bool all_out = true;
    for(int h = 0; h < 256; h += 8) {
      if(oklch_color_t(160, 255, (uint8_t)h, 255).in_gamut()) all_out = false;
    }
    CHECK(all_out);

    // A grey is always showable, whatever its lightness.
    bool all_in = true;
    for(int l = 0; l <= 255; l += 15) {
      if(!oklch_color_t((uint8_t)l, 0, 0, 255).in_gamut()) all_in = false;
    }
    CHECK(all_in);

    // Nothing authored as channels can name a colour outside the gamut.
    CHECK(rgb_color_t(255, 0, 0, 255).in_gamut());
    CHECK(hsv_color_t(200, 255, 255, 255).in_gamut());
  }

  printf("palette: fitting reduces chroma and leaves the rest alone\n");
  {
    color_t wanted = oklch_color_t(160, 255, 21, 255);
    CHECK(!wanted.in_gamut());

    color_t fitted = wanted.fit();
    CHECK(fitted.in_gamut());
    CHECK(fitted.space() == COLOR_OKLCH);
    CHECK(fitted.l() == 160);       // lightness held
    CHECK(fitted.h() == 21);        // hue held
    CHECK(fitted.c() < 255);        // chroma is what gave way
    CHECK(fitted.a() == 255);

    // An already-showable colour is handed straight back.
    color_t modest = oklch_color_t(160, 40, 21, 255);
    CHECK(modest.fit() == modest);
    CHECK(modest.fit().c() == 40);
  }

  printf("palette: fitting is a no-op on a colour that cannot be out of gamut\n");
  {
    color_t rgb = rgb_color_t(200, 100, 50, 255);
    CHECK(rgb.fit() == rgb);
    CHECK(rgb.fit().space() == COLOR_RGB);
  }

  printf("palette: the chroma ceiling is the boundary it says it is\n");
  {
    bool boundary = true;
    for(int l = 20; l <= 235; l += 15) {
      for(int h = 0; h < 256; h += 16) {
        uint8_t ceiling = color_t::max_chroma((uint8_t)l, (uint8_t)h);
        if(!oklch_color_t((uint8_t)l, ceiling, (uint8_t)h, 255).in_gamut()) boundary = false;
        if(ceiling < 255 &&
           oklch_color_t((uint8_t)l, ceiling + 1, (uint8_t)h, 255).in_gamut()) boundary = false;
      }
    }
    CHECK_MSG(boundary, "max_chroma is not the last chroma that fits");
  }

  printf("palette: the gamut is lopsided, which is the whole reason to fit\n");
  {
    // Yellow carries far more chroma at high lightness than blue does. A harmony
    // that rotates hue at fixed chroma is walking across this.
    uint8_t yellow = color_t::max_chroma(220, 75);
    uint8_t blue = color_t::max_chroma(220, 190);
    CHECK_MSG(yellow > blue * 2, "yellow should outreach blue at high lightness");

    // Near white a channel is already close to the ceiling, so almost no chroma
    // fits and the ceiling collapses.
    CHECK(color_t::max_chroma(253, 0) < 32);
    CHECK(color_t::max_chroma(255, 0) == 0);

    // Near black it does not, and that is not a bug: chroma there changes the
    // output by less than a byte, so nothing is being clipped. The colour is
    // black whatever is asked for, which is what the resolved channels say.
    color_t nearly_black = oklch_color_t(2, color_t::max_chroma(2, 0), 0, 255);
    CHECK(nearly_black.in_gamut());
    CHECK(nearly_black.r() < 8 && nearly_black.g() < 8 && nearly_black.b() < 8);
  }

  printf("palette: rotating moves the hue and nothing else\n");
  {
    color_t base = oklch_color_t(160, 60, 21, 255);
    color_t turned = base.rotate(85);
    CHECK(turned.space() == COLOR_OKLCH);
    CHECK(turned.h() == 21 + 85);
    CHECK(turned.l() == 160);
    CHECK(turned.c() == 60);
    CHECK(turned.a() == 255);

    // The wheel wraps rather than clamping, unlike with_component.
    CHECK(base.rotate(256).h() == 21);
    CHECK(base.rotate(-42).h() == (uint8_t)(21 - 42));
    CHECK(oklch_color_t(160, 60, 250, 255).rotate(20).h() == 14);

    // An RGB colour has no hue to move, so it is read in OKLCH first.
    color_t from_rgb = rgb_color_t(255, 0, 0, 255).rotate(128);
    CHECK(from_rgb.space() == COLOR_OKLCH);
    CHECK(hue_apart(from_rgb.h(), 21 + 128) <= 1);
  }

  printf("palette: saturating moves the chroma\n");
  {
    color_t base = oklch_color_t(160, 60, 21, 255);
    CHECK(base.saturate(40).c() == 100);
    CHECK(base.saturate(-40).c() == 20);
    CHECK(base.saturate(0).c() == 60);
    CHECK(base.saturate(-255).c() == 0);
    CHECK(base.saturate(40).l() == 160 && base.saturate(40).h() == 21);

    // HSV has its own saturation axis and keeps it.
    color_t hsv = hsv_color_t(100, 128, 200, 255);
    CHECK(hsv.saturate(40).space() == COLOR_HSV);
    CHECK(hsv.saturate(40).s() == 168);
    CHECK(hsv.saturate(400).s() == 255);   // clamps

    // Saturating past the gamut fits instead of clipping.
    CHECK(oklch_color_t(160, 60, 21, 255).saturate(255).in_gamut());
  }

  printf("palette: a complement is exactly opposite\n");
  {
    color_t base = oklch_color_t(160, 60, 21, 255);
    color_t out[color_t::max_harmony];

    CHECK(base.harmony(SCHEME_COMPLEMENT, out) == 2);
    CHECK(out[0].h() == 21);         // the colour it was asked about comes first
    CHECK(out[1].h() == 21 + 128);
    CHECK(out[0].l() == 160 && out[1].l() == 160);
  }

  printf("palette: every scheme returns its documented colours\n");
  {
    struct { color_scheme_t scheme; int count; } schemes[] = {
      { SCHEME_COMPLEMENT, 2 }, { SCHEME_SPLIT, 3 }, { SCHEME_TRIAD, 3 },
      { SCHEME_TETRAD, 4 }, { SCHEME_SQUARE, 4 }, { SCHEME_ANALOGOUS, 3 },
    };

    color_t base = oklch_color_t(160, 90, 21, 255);
    bool counted = true, fitted = true, first = true, distinct = true;
    for(auto &s : schemes) {
      color_t out[color_t::max_harmony];
      int n = base.harmony(s.scheme, out);
      if(n != s.count) counted = false;
      if(out[0].h() != base.h()) first = false;
      for(int i = 0; i < n; i++) {
        if(!out[i].in_gamut()) fitted = false;
        // Every hue in a scheme is a different one.
        for(int j = i + 1; j < n; j++) if(out[i].h() == out[j].h()) distinct = false;
      }
    }
    CHECK(counted);
    CHECK_MSG(first, "the base colour should come first");
    CHECK_MSG(fitted, "a harmony handed back a colour the screen cannot show");
    CHECK(distinct);
  }

  printf("palette: a triad and a square land where the wheel says\n");
  {
    color_t base = oklch_color_t(160, 60, 0, 255);
    color_t out[color_t::max_harmony];

    // 120 degrees is 85.33 counts, so a third of a turn is a count out at most.
    base.harmony(SCHEME_TRIAD, out);
    CHECK(hue_apart(out[1].h(), 85) <= 1);
    CHECK(hue_apart(out[2].h(), 171) <= 1);

    // Quarters divide the byte exactly.
    base.harmony(SCHEME_SQUARE, out);
    CHECK(out[1].h() == 64 && out[2].h() == 128 && out[3].h() == 192);
  }

  printf("palette: a harmony off an RGB colour matches one off its OKLCH\n");
  {
    color_t as_rgb = rgb_color_t(200, 60, 40, 255);
    color_t as_oklch = as_rgb.to_oklch();

    color_t from_rgb[color_t::max_harmony], from_oklch[color_t::max_harmony];
    int a = as_rgb.harmony(SCHEME_TRIAD, from_rgb);
    int b = as_oklch.harmony(SCHEME_TRIAD, from_oklch);

    CHECK(a == b);
    bool same = true;
    for(int i = 0; i < a; i++) if(from_rgb[i] != from_oklch[i]) same = false;
    CHECK(same);
  }

  printf("palette: a tonal ladder runs black to white at one hue\n");
  {
    color_t base = oklch_color_t(160, 90, 21, 255);
    color_t ladder[13];
    base.tones(ladder, 13);

    CHECK(ladder[0].l() == 0);        // starts at black
    CHECK(ladder[12].l() == 255);     // ends at white

    bool rising = true, same_hue = true, fitted = true;
    for(int i = 0; i < 13; i++) {
      if(ladder[i].h() != base.h()) same_hue = false;
      if(!ladder[i].in_gamut()) fitted = false;
      if(i > 0 && ladder[i].l() <= ladder[i - 1].l()) rising = false;
    }
    CHECK_MSG(rising, "lightness should climb the ladder");
    CHECK(same_hue);
    CHECK_MSG(fitted, "a tone was left outside the gamut");

    // Luminance climbs with it, which is what makes the ladder useful for
    // picking a readable pair.
    bool brighter = true;
    for(int i = 1; i < 13; i++) {
      if(ladder[i].luminance() <= ladder[i - 1].luminance()) brighter = false;
    }
    CHECK(brighter);

    // The ends are black and white however much chroma they nominally carry -
    // near white there is no room for any, and near black it makes no visible
    // difference, which is why fit() leaves it be there.
    CHECK(ladder[0].r() < 8 && ladder[0].g() < 8 && ladder[0].b() < 8);
    CHECK(ladder[12].r() > 247 && ladder[12].g() > 247 && ladder[12].b() > 247);
    CHECK(ladder[12].c() < 32);
  }

  printf("palette: a ladder of one is the colour itself\n");
  {
    color_t base = oklch_color_t(160, 40, 21, 255);
    color_t one[1];
    base.tones(one, 1);
    CHECK(one[0].l() == 160 && one[0].h() == 21);
  }

  printf("palette: readable_on reaches the ratio it is asked for\n");
  {
    color_t white = rgb_color_t(255, 255, 255, 255);
    color_t black = rgb_color_t(0, 0, 0, 255);

    // A mid-tone that fails against white has to go darker to pass.
    color_t mid = oklch_color_t(180, 60, 21, 255);
    CHECK(mid.contrast(white) < 4.5f);

    color_t fixed = mid.readable_on(white, 4.5f);
    CHECK_MSG(fixed.contrast(white) >= 4.5f, "readable_on missed the ratio");
    CHECK(fixed.l() < mid.l());          // it went darker, against a light background
    CHECK(fixed.h() == mid.h());         // and kept its hue
    CHECK(fixed.in_gamut());

    // Against black the same colour goes the other way.
    color_t dark = oklch_color_t(60, 60, 21, 255);
    CHECK(dark.contrast(black) < 4.5f);
    color_t lifted = dark.readable_on(black, 4.5f);
    CHECK(lifted.contrast(black) >= 4.5f);
    CHECK(lifted.l() > dark.l());
  }

  printf("palette: readable_on leaves a colour that already passes alone\n");
  {
    color_t white = rgb_color_t(255, 255, 255, 255);
    color_t black = rgb_color_t(0, 0, 0, 255);
    CHECK(black.readable_on(white, 4.5f) == black);
    CHECK(black.readable_on(white, 4.5f).space() == COLOR_RGB);   // not converted
    CHECK(black.readable_on(white, 21.0f) == black);
  }

  printf("palette: readable_on holds every ratio it is asked for\n");
  {
    color_t backgrounds[] = {
      rgb_color_t(255, 255, 255, 255), rgb_color_t(0, 0, 0, 255),
      rgb_color_t(128, 128, 128, 255), rgb_color_t(30, 60, 120, 255),
    };
    float ratios[] = { 3.0f, 4.5f, 7.0f };

    bool met = true;
    for(auto &bg : backgrounds) {
      for(float ratio : ratios) {
        for(int h = 0; h < 256; h += 32) {
          color_t want = oklch_color_t(140, 50, (uint8_t)h, 255);
          color_t got = want.readable_on(bg, ratio);
          // Either it cleared the ratio, or no lightness at that hue could.
          float best = got.contrast(bg);
          if(best < ratio) {
            float top = oklch_color_t(255, 0, (uint8_t)h, 255).contrast(bg);
            float bottom = oklch_color_t(0, 0, (uint8_t)h, 255).contrast(bg);
            if(top >= ratio || bottom >= ratio) met = false;
          }
        }
      }
    }
    CHECK_MSG(met, "readable_on settled for less than it could reach");
  }

  printf("palette: readable_on returns the best it can when it cannot win\n");
  {
    // Mid grey is the hardest background there is: nothing clears 7 against it.
    color_t grey = rgb_color_t(119, 119, 119, 255);
    color_t attempt = oklch_color_t(140, 50, 21, 255).readable_on(grey, 21.0f);

    CHECK(attempt.contrast(grey) < 21.0f);   // it could not be done
    // But it went as far as it could rather than giving up where it started.
    CHECK(attempt.contrast(grey) > oklch_color_t(140, 50, 21, 255).contrast(grey));
    CHECK(attempt.in_gamut());
  }

  printf("palette: a whole interface comes out of one colour\n");
  {
    // The point of all of it: a theme colour, a tonal ladder for surfaces, and
    // text that is readable on whichever surface it lands on.
    color_t theme = rgb_color_t(90, 125, 206, 255);   // the palette's blue

    color_t surfaces[9];
    theme.tones(surfaces, 9);

    bool readable = true;
    for(int i = 0; i < 9; i++) {
      color_t text = theme.readable_on(surfaces[i], 4.5f);
      if(text.contrast(surfaces[i]) < 4.5f) readable = false;
      if(!text.in_gamut()) readable = false;
    }
    CHECK_MSG(readable, "some surface in the ladder had no readable text");

    // And the accents are distinguishable from the theme they came from.
    color_t accents[color_t::max_harmony];
    int n = theme.harmony(SCHEME_TRIAD, accents);
    bool distinct = true;
    for(int i = 1; i < n; i++) {
      if(accents[0].difference(accents[i]) < 5.0f) distinct = false;
    }
    CHECK_MSG(distinct, "a triad should be obviously three colours");
  }

  printf("ramp: the count asked for is the count returned\n");
  {
    const float pos[2] = { 0.0f, 1.0f };
    const color_t stops[2] = { rgb_color_t(255, 0, 0, 255), rgb_color_t(0, 0, 255, 255) };

    for(int count : { 1, 2, 3, 65, 256 }) {
      color_t out[256];
      sample_ramp(out, count, pos, stops, 2);
      // The ends are the stops themselves, whatever the count.
      CHECK_MSG(out[count - 1] == stops[1], "the last entry is the last stop");
      if(count > 1) CHECK_MSG(out[0] == stops[0], "the first entry is the first stop");
    }
  }

  printf("ramp: a stop lands exactly on an entry\n");
  {
    // 0.45 of 64 steps is entry 29 to the nearest whole one, and the colour
    // there is the stop itself rather than something interpolated past it.
    const float pos[4] = { 0.0f, 0.45f, 0.72f, 1.0f };
    const color_t stops[4] = {
      rgb_color_t(255, 0, 0, 255), rgb_color_t(0, 255, 0, 255),
      rgb_color_t(0, 0, 255, 255), rgb_color_t(255, 255, 0, 255),
    };

    color_t out[65];
    sample_ramp(out, 65, pos, stops, 4);
    CHECK(out[0] == stops[0]);
    CHECK(out[29] == stops[1]);
    CHECK(out[46] == stops[2]);
    CHECK(out[64] == stops[3]);
  }

  printf("ramp: two OKLCH stops ramp through OKLCH\n");
  {
    const float pos[2] = { 0.0f, 1.0f };
    const color_t ok[2] = { oklch_color_t(160, 90, 21, 255), oklch_color_t(160, 90, 149, 255) };
    color_t out[9];
    sample_ramp(out, 9, pos, ok, 2);

    // Held lightness and chroma the whole way, which sRGB interpolation between
    // the same endpoints cannot do - it sags through a muddy middle.
    bool held = true;
    for(int i = 0; i < 9; i++) {
      if(out[i].space() != COLOR_OKLCH) held = false;
      if(out[i].l() != 160 || out[i].c() != 90) held = false;
    }
    CHECK(held);
    CHECK(out[4].h() == 85);   // half way round the short arc
  }

  printf("ramp: sampling 256 ways is exactly what a gradient builds\n");
  {
    // The direct proof that color.ramp and a gradient brush are one
    // implementation: same stops, same table, entry for entry.
    const float pos[4] = { 0.0f, 0.25f, 0.6f, 1.0f };
    const color_t stops[4] = {
      oklch_color_t(90, 70, 21, 255), rgb_color_t(0, 255, 0, 255),
      oklch_color_t(200, 40, 149, 128), rgb_color_t(255, 255, 0, 255),
    };

    gradient_brush_t g(GRADIENT_LINEAR, 0, 0, 1, 0, pos, stops, 4, nullptr);

    pixel_t sampled[256];
    sample_ramp(sampled, 256, pos, stops, 4);

    bool identical = true;
    for(int i = 0; i < 256; i++) if(sampled[i] != g.lut[i]) identical = false;
    CHECK_MSG(identical, "the ramp and the gradient table disagree");
  }

  printf("ramp: the awkward stop lists behave as they do for a gradient\n");
  {
    color_t out[32];

    // No stops is a run of nothing.
    sample_ramp(out, 32, nullptr, nullptr, 0);
    bool empty = true;
    for(int i = 0; i < 32; i++) if(out[i] != color_t()) empty = false;
    CHECK(empty);

    // One stop fills it.
    const float one_pos[1] = { 0.5f };
    const color_t one[1] = { rgb_color_t(10, 20, 30, 255) };
    sample_ramp(out, 32, one_pos, one, 1);
    bool filled = true;
    for(int i = 0; i < 32; i++) if(out[i] != one[0]) filled = false;
    CHECK(filled);

    // Offsets outside 0-1, and going backwards, are clamped and coerced.
    const float wild[4] = { -1.0f, 0.6f, 0.4f, 2.0f };
    const color_t four[4] = {
      rgb_color_t(255, 0, 0, 255), rgb_color_t(0, 255, 0, 255),
      rgb_color_t(0, 0, 255, 255), rgb_color_t(255, 255, 0, 255),
    };
    sample_ramp(out, 32, wild, four, 4);
    CHECK(out[0] == four[0]);
    CHECK(out[31] == four[3]);
  }

  printf("ramp: the middle of a segment is the middle of the mix\n");
  {
    // A seven-entry ramp puts entry 3 exactly half way along a span of six,
    // which only lands on mix's own midpoint if the weight is rounded rather
    // than truncated on the way in. Truncating costs a count here and a count
    // there across every segment, which is visible on a long shallow ramp.
    const float pos[2] = { 0.0f, 1.0f };
    const color_t stops[2] = { rgb_color_t(0, 0, 0, 255), rgb_color_t(255, 255, 255, 255) };

    color_t out[7];
    sample_ramp(out, 7, pos, stops, 2);
    CHECK(out[3] == stops[0].mix(stops[1], 128));
    CHECK(out[1] == stops[0].mix(stops[1], 43));
    CHECK(out[5] == stops[0].mix(stops[1], 213));
  }

  printf("ramp: past the ends it pads rather than repeating\n");
  {
    const float pos[2] = { 0.25f, 0.75f };
    const color_t stops[2] = { rgb_color_t(255, 0, 0, 255), rgb_color_t(0, 0, 255, 255) };
    color_t out[65];
    sample_ramp(out, 65, pos, stops, 2);

    bool padded = true;
    for(int i = 0; i <= 16; i++) if(out[i] != stops[0]) padded = false;
    for(int i = 48; i < 65; i++) if(out[i] != stops[1]) padded = false;
    CHECK(padded);
  }
}
