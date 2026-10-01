// The RGB565 framebuffer format: a platform detail that must be invisible.
//
// The contract under test: drawing X to a 565 image leaves exactly
// pack565(drawing X to an RGBA image), whenever every input colour is
// 565-clean (each channel survives the pack/unpack round trip) and each pixel
// is composited at most once. Then the staging shim's unpack reproduces the
// RGBA path's destination bit-for-bit, the blend arithmetic is the same code,
// and only the final pack differs. Multi-layer compositing accumulates
// quantisation instead, so it gets a small tolerance.

#include "test.hpp"
#include "image.hpp"
#include "brush.hpp"

using namespace picovector;

// 565-clean channels: low bits already the replication of the high ones.
static uint32_t clean(uint8_t r, uint8_t g, uint8_t b) {
  return pv_565_to_8888(pv_8888_to_565(0xff000000u | (b << 16) | (g << 8) | r));
}

static void fill_pair(image_t &a, image_t &b, uint32_t c) {
  color_brush_t br(rgb_color_t(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff, 255));
  a.brush(&br); a.rectangle(a.bounds());
  b.brush(&br); b.rectangle(b.bounds());
}

// byte-exact: every 565 pixel equals the packed RGBA pixel
static int diff_exact(image_t &rgba, image_t &fb) {
  int w = (int)rgba.bounds().w, h = (int)rgba.bounds().h, bad = 0;
  for(int y = 0; y < h; y++)
    for(int x = 0; x < w; x++) {
      uint16_t want = pv_8888_to_565(*(uint32_t *)rgba.ptr(x, y));
      uint16_t got = *(uint16_t *)fb.ptr(x, y);
      if(want != got) bad++;
    }
  return bad;
}

// tolerant: unpacked channels within `tol`
static int diff_tol(image_t &rgba, image_t &fb, int tol) {
  int w = (int)rgba.bounds().w, h = (int)rgba.bounds().h, bad = 0;
  for(int y = 0; y < h; y++)
    for(int x = 0; x < w; x++) {
      uint32_t a = *(uint32_t *)rgba.ptr(x, y);
      uint32_t b = pv_565_to_8888(*(uint16_t *)fb.ptr(x, y));
      for(int s = 0; s < 24; s += 8) {
        int d = (int)((a >> s) & 0xff) - (int)((b >> s) & 0xff);
        if(d < -tol || d > tol) { bad++; break; }
      }
    }
  return bad;
}

static void noise_pair(image_t &a, image_t &fb) {
  uint32_t seed = 12345;
  int w = (int)fb.bounds().w, h = (int)fb.bounds().h;
  for(int y = 0; y < h; y++)
    for(int x = 0; x < w; x++) {
      seed = seed * 1664525u + 1013904223u;
      uint16_t p = (uint16_t)(seed >> 16);
      *(uint16_t *)fb.ptr(x, y) = p;
      *(uint32_t *)a.ptr(x, y) = pv_565_to_8888(p);
    }
}

static void brush_pair(image_t &a, image_t &fb, brush_t *br) {
  a.brush(br); a.rectangle(a.bounds()); a.brush(nullptr);
  fb.brush(br); fb.rectangle(fb.bounds()); fb.brush(nullptr);
}

void test_rgb565() {
  const uint32_t BG = clean(32, 48, 64), INK = clean(248, 128, 64);

  printf("rgb565: bytes per pixel, get/set round trip\n");
  {
    image_t fb(32, 32, RGB565);
    CHECK(fb.bytes_per_pixel() == 2);
    color_brush_t br(rgb_color_t((INK) & 0xff, (INK >> 8) & 0xff, (INK >> 16) & 0xff, 255));
    fb.brush(&br);
    fb.put(3, 4);
    CHECK(fb.get(3, 4) == INK);        // clean colour survives exactly
  }

  printf("rgb565: solid fill matches packed RGBA fill exactly\n");
  {
    image_t a(64, 48), fb(64, 48, RGB565);
    fill_pair(a, fb, BG);
    CHECK(diff_exact(a, fb) == 0);
  }

  printf("rgb565: a fill at reduced image alpha matches exactly\n");
  {
    image_t a(64, 48), fb(64, 48, RGB565);
    fill_pair(a, fb, BG);
    a.alpha(128); fb.alpha(128);
    fill_pair(a, fb, INK);
    CHECK(diff_exact(a, fb) == 0);
  }

  printf("rgb565: native translucent fill over noise matches exactly\n");
  {
    image_t a(64, 48), fb(64, 48, RGB565);
    noise_pair(a, fb);
    color_brush_t br(rgb_color_t(200, 90, 30, 170));
    brush_pair(a, fb, &br);
    CHECK(diff_exact(a, fb) == 0);
  }

  printf("rgb565: native crt over noise matches exactly\n");
  {
    image_t a(96, 72), fb(96, 72, RGB565);
    noise_pair(a, fb);
    crt_brush_t br(3, 70, 200);
    brush_pair(a, fb, &br);
    CHECK(diff_exact(a, fb) == 0);
  }

  printf("rgb565: native duotone over noise matches exactly\n");
  {
    image_t a(64, 48), fb(64, 48, RGB565);
    noise_pair(a, fb);
    duotone_brush_t br(rgb_color_t(2, 12, 6, 255), rgb_color_t(150, 255, 170, 255));
    brush_pair(a, fb, &br);
    CHECK(diff_exact(a, fb) == 0);
  }

  printf("rgb565: AA circle over a clean background matches exactly\n");
  {
    image_t a(64, 64), fb(64, 64, RGB565);
    fill_pair(a, fb, BG);
    color_brush_t br(rgb_color_t((INK) & 0xff, (INK >> 8) & 0xff, (INK >> 16) & 0xff, 255));
    a.brush(&br); a.antialias(X4); a.circle(vec2_t(32, 32), 20);
    fb.brush(&br); fb.antialias(X4); fb.circle(vec2_t(32, 32), 20);
    CHECK(diff_exact(a, fb) == 0);
  }

  printf("rgb565: triangle and line match exactly\n");
  {
    image_t a(64, 64), fb(64, 64, RGB565);
    fill_pair(a, fb, BG);
    color_brush_t br(rgb_color_t((INK) & 0xff, (INK >> 8) & 0xff, (INK >> 16) & 0xff, 255));
    a.brush(&br); a.antialias(X4);
    fb.brush(&br); fb.antialias(X4);
    a.triangle(vec2_t(4, 60), vec2_t(60, 50), vec2_t(30, 6));
    fb.triangle(vec2_t(4, 60), vec2_t(60, 50), vec2_t(30, 6));
    CHECK(diff_exact(a, fb) == 0);
  }

  printf("rgb565: blits in and out match exactly\n");
  {
    image_t sprite(16, 16);
    color_brush_t br(rgb_color_t((INK) & 0xff, (INK >> 8) & 0xff, (INK >> 16) & 0xff, 255));
    sprite.brush(&br); sprite.rectangle(sprite.bounds());
    image_t a(64, 64), fb(64, 64, RGB565);
    fill_pair(a, fb, BG);
    sprite.blit(&a, vec2_t(5, 7));
    sprite.blit(&fb, vec2_t(5, 7));
    sprite.blit(&a, rect_t(0, 0, 16, 16), rect_t(30, 30, 28, 22), NEAREST);
    sprite.blit(&fb, rect_t(0, 0, 16, 16), rect_t(30, 30, 28, 22), NEAREST);
    CHECK(diff_exact(a, fb) == 0);

    // 565 as a source: copy the framebuffer into an RGBA image and compare
    // against the unpack of the framebuffer itself.
    image_t shot(64, 64);
    fb.blit(&shot, vec2_t(0, 0));
    int bad = 0;
    for(int y = 0; y < 64; y++)
      for(int x = 0; x < 64; x++)
        if((*(uint32_t *)shot.ptr(x, y) & 0xffffffu) != (fb.get(x, y) & 0xffffffu)) bad++;
    CHECK(bad == 0);
  }

  printf("rgb565: layered AA compositing stays within quantisation tolerance\n");
  {
    image_t a(64, 64), fb(64, 64, RGB565);
    fill_pair(a, fb, BG);
    for(int i = 0; i < 4; i++) {
      uint32_t ci = clean(60 * i + 40, 255 - 50 * i, 90 + 30 * i);
      color_brush_t br(rgb_color_t(ci & 0xff, (ci >> 8) & 0xff, (ci >> 16) & 0xff, 255));
      a.brush(&br); a.antialias(X4); a.circle(vec2_t(24 + i * 5, 28 + i * 3), 14);
      fb.brush(&br); fb.antialias(X4); fb.circle(vec2_t(24 + i * 5, 28 + i * 3), 14);
    }
    CHECK(diff_tol(a, fb, 12) == 0);
  }
}
