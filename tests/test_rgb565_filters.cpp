// Whole-image filters and neighbourhood brushes on an RGB565 target.
//
// These read and write the 565 pixels themselves rather than through the
// staging shim, so the contract is checked directly: the same scene in an
// RGBA image and a 565 image, the same operation on each, and the packed
// RGBA result compared with the 565 one. The scene is quantised to 565 first
// so both start from identical pixels. Most cases match exactly. Two kinds
// get a tolerance, and each is also checked exact where the cause is removed:
// brushes that read rows they have already written in place (blur, oilpaint)
// read them back quantised on 565, and filters chained inside one call
// (edgeglow, cga) hand the next stage a quantised image.

#include <functional>

#include "test.hpp"
#include "image.hpp"
#include "brush.hpp"

using namespace picovector;

namespace {

  struct pair_t {
    int w, h;
    image_t a, fb;
    pair_t(int w, int h) : w(w), h(h), a(w, h), fb(w, h, RGB565) {}
  };

  // a smooth gradient with AA shapes over it, then quantised into both images
  void scene(pair_t &p) {
    float pos[3] = { 0.0f, 0.5f, 1.0f };
    color_t stops[3] = { rgb_color_t(250, 30, 20, 255), rgb_color_t(30, 220, 90, 255), rgb_color_t(20, 40, 250, 255) };
    gradient_brush_t g(GRADIENT_LINEAR, 0, 0, (float)p.w, (float)p.h, pos, stops, 3, nullptr);
    p.a.brush(&g); p.a.rectangle(p.a.bounds());
    color_brush_t c1(rgb_color_t(255, 255, 240, 255)), c2(rgb_color_t(10, 10, 30, 200));
    p.a.antialias(X4);
    p.a.brush(&c1); p.a.circle(vec2_t(p.w * 0.3f, p.h * 0.4f), p.h / 4);
    p.a.brush(&c2); p.a.circle(vec2_t(p.w * 0.65f, p.h * 0.6f), p.h / 5);
    p.a.brush(nullptr); p.a.antialias(OFF);
    for(int y = 0; y < p.h; y++)
      for(int x = 0; x < p.w; x++) {
        uint16_t q = pv_8888_to_565(*(uint32_t *)p.a.ptr(x, y));
        *(uint16_t *)p.fb.ptr(x, y) = q;
        *(uint32_t *)p.a.ptr(x, y) = pv_565_to_8888(q);
      }
  }

  // largest per-channel difference, in 8-bit units, after packing the RGBA
  // result; `bad` counts 565 pixels that differ at all
  int max_err(pair_t &p, int &bad) {
    int worst = 0; bad = 0;
    for(int y = 0; y < p.h; y++)
      for(int x = 0; x < p.w; x++) {
        uint32_t want = pv_565_to_8888(pv_8888_to_565(*(uint32_t *)p.a.ptr(x, y)));
        uint32_t got = pv_565_to_8888(*(uint16_t *)p.fb.ptr(x, y));
        if(want != got) bad++;
        for(int s = 0; s < 24; s += 8) {
          int d = (int)((want >> s) & 0xff) - (int)((got >> s) & 0xff);
          if(d < 0) d = -d;
          if(d > worst) worst = d;
        }
      }
    return worst;
  }

  // what a 565 image holds between two stages
  void quantise(image_t &img) {
    if(img.pixel_format() != RGBA8888) return;
    for(int y = 0; y < (int)img.bounds().h; y++)
      for(int x = 0; x < (int)img.bounds().w; x++) {
        uint32_t *p = (uint32_t *)img.ptr(x, y);
        *p = pv_565_to_8888(pv_8888_to_565(*p));
      }
  }

  // a copy of the 565 image before the operation, to prove it did something
  int changed(pair_t &p, const std::vector<uint16_t> &before) {
    int n = 0;
    for(int y = 0; y < p.h; y++)
      for(int x = 0; x < p.w; x++)
        if(*(uint16_t *)p.fb.ptr(x, y) != before[(size_t)y * p.w + x]) n++;
    return n;
  }

  void run(const char *name, int w, int h, int tol, std::function<void(image_t &)> op) {
    pair_t p(w, h);
    scene(p);
    std::vector<uint16_t> before((size_t)w * h);
    for(int y = 0; y < h; y++)
      for(int x = 0; x < w; x++) before[(size_t)y * w + x] = *(uint16_t *)p.fb.ptr(x, y);
    op(p.a);
    op(p.fb);
    int bad, err = max_err(p, bad);
    printf("rgb565 filters: %-28s %dx%d  max err %2d, %d/%d px differ\n", name, w, h, err, bad, w * h);
    char buf[96];
    snprintf(buf, sizeof(buf), "%s: max err %d", name, err);
    CHECK_MSG(err <= tol, buf);
    CHECK_MSG(changed(p, before) > 0, name);
  }

  // brush through the solid path (rectangle, AA off) and the masked path (AA circle)
  void brush_both(const char *name, int tol, brush_t *b) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s solid", name);
    run(buf, 97, 71, tol, [b](image_t &img) {
      img.antialias(OFF); img.brush(b); img.rectangle(rect_t(3, 5, 89, 61)); img.brush(nullptr);
    });
    snprintf(buf, sizeof(buf), "%s masked", name);
    run(buf, 97, 71, tol, [b](image_t &img) {
      img.antialias(X4); img.brush(b); img.circle(vec2_t(47, 35), 30); img.brush(nullptr);
      img.antialias(OFF);
    });
  }

}

void test_rgb565_filters() {
  // whole-image filters
  for(int i = 0; i < 2; i++) {
    // 320x240 borrows the working buffer for blur's low bits; 400x240 doesn't fit
    int w = i ? 400 : 320, h = 240;
    run("blur 4", w, h, 0, [](image_t &img) { img.blur(4.0f); });
  }
  run("blur 1.5 small", 61, 37, 0, [](image_t &img) { img.blur(1.5f); });
  run("blur clipped", 97, 71, 0, [](image_t &img) { img.clip(rect_t(10, 7, 50, 40)); img.blur(3.0f); });
  run("bloom", 320, 240, 0, [](image_t &img) { img.bloom(100, 150, 4.0f); });
  run("wave", 320, 240, 0, [](image_t &img) { img.wave(4, 4); });
  run("wave bilinear", 320, 240, 0, [](image_t &img) { img.wave(6, 9, 1.0f, true); });
  run("wave vertical 40", 97, 71, 0, [](image_t &img) { img.wave(0, 40); });
  run("wave clipped", 97, 71, 0, [](image_t &img) { img.clip(rect_t(10, 7, 50, 40)); img.wave(5, 5, 1.0f, true); });
  run("zoom", 320, 240, 0, [](image_t &img) { img.zoom(200); });
  run("zoom out", 97, 71, 0, [](image_t &img) { img.zoom(-200); });
  run("zoom clipped", 97, 71, 0, [](image_t &img) { img.clip(rect_t(60, 7, 30, 40)); img.zoom(255); });

  // edgeglow's Sobel stage is exact; its bloom then reads the quantised edges
  run("edgeglow", 320, 240, 16, [](image_t &img) { img.edgeglow(220); });
  run("edgeglow clipped", 97, 71, 16, [](image_t &img) { img.clip(rect_t(10, 7, 50, 40)); img.edgeglow(220); });
  run("cga", 97, 71, 24, [](image_t &img) { img.cga(); });
  run("cga stages, quantised between", 97, 71, 0, [](image_t &img) {
    uint32_t pal[4] = { 0xff000000u, 0xffffff55u, 0xffff55ffu, 0xffffffffu };
    palette_dither_brush_t d(pal, 4, 16);
    img.brush(&d); img.rectangle(img.bounds()); quantise(img);
    img.bloom(50, 220, 4); quantise(img);
    crt_brush_t c(2, 70);
    img.brush(&c); img.rectangle(img.bounds()); img.brush(nullptr);
  });

  // neighbourhood brushes
  { pixelate_brush_t b(5); brush_both("pixelate 5", 0, &b); }
  { chromatic_brush_t b(4); brush_both("chromatic 4", 0, &b); }
  { glitch_brush_t b(200); brush_both("glitch", 0, &b); }
  { oilpaint_brush_t b(3, 200); brush_both("oilpaint 3", 24, &b); }
  { blur_brush_t b(1); brush_both("blur brush 1", 9, &b); }
  { blur_brush_t b(6); brush_both("blur brush 6", 9, &b); }
  { blur_brush_t b(40); brush_both("blur brush 40", 9, &b); }
  run("filter glitch", 97, 71, 0, [](image_t &img) { img.glitch(120); });
  run("filter oilpaint", 97, 71, 24, [](image_t &img) { img.oilpaint(2, 255); });
  run("filter chromatic", 97, 71, 0, [](image_t &img) { img.chromatic(5); });

  // one row inside one 64 px chunk reads nothing it has written: exact
  { oilpaint_brush_t b(3, 200);
    run("oilpaint 3 one row", 97, 71, 0, [&](image_t &img) { img.brush(&b); img.rectangle(rect_t(3, 30, 60, 1)); img.brush(nullptr); }); }
  { blur_brush_t b(6);
    run("blur brush 6 one row", 97, 71, 0, [&](image_t &img) { img.brush(&b); img.rectangle(rect_t(3, 30, 60, 1)); img.brush(nullptr); }); }
  { blur_brush_t b(40);
    run("blur brush 40 one row", 97, 71, 0, [&](image_t &img) { img.brush(&b); img.rectangle(rect_t(3, 30, 60, 1)); img.brush(nullptr); }); }
}
