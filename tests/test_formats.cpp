// The pixel formats other than RGBA8888: RGBA4444, PAL8, LUM8 and MASK8.
//
// Brushes and blits write RGBA8888. A target in another format is drawn a
// converted row at a time (brush.cpp, blit.cpp), so the test for one is that
// it ends up holding what the same drawing left in an RGBA8888 image, packed.
// The scenes are grey, where that holds to the bit: every channel blends the
// same, so luminance commutes with the blend.

#include "test.hpp"
#include "helpers.hpp"

using namespace picovector;
using namespace pvtest;

namespace {

  // wider than the 256 pixel conversion chunk, so spans straddle its edge
  const int W = 300, H = 40;

  // Solid and masked spans, opaque and blended, a patterned brush and a line
  void scene(image_t &img) {
    color_brush_t dark(rgb_color_t(30, 30, 30, 255));
    color_brush_t veil(rgb_color_t(255, 255, 255, 128));
    color_brush_t mid(rgb_color_t(90, 90, 90, 200));
    pattern_brush_t pat(rgb_color_t(255, 255, 255, 255), rgb_color_t(0, 0, 0, 255), 5);

    img.antialias(OFF);
    img.brush(&dark);
    img.rectangle(rect_t(4, 4, 290, 10));
    img.brush(&veil);
    img.rectangle(rect_t(0, 8, 300, 12));
    img.brush(&pat);
    img.rectangle(rect_t(10, 22, 280, 9));
    img.brush(&mid);
    img.line(vec2_t(2, 2), vec2_t(297, 37));
    img.circle(vec2_t(150, 20), 15);

    img.antialias(X4);
    img.brush(&dark);
    img.shape(circle(60, 20, 17.5f));
    img.brush(&veil);
    img.shape(rectangle(100.5f, 3.25f, 190.5f, 30.5f));
    img.antialias(OFF);
  }

  int count_diff(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
    int n = 0;
    for(size_t i = 0; i < a.size(); i++) if(a[i] != b[i]) n++;
    return n;
  }

}

void test_formats() {
  printf("formats: a pixel is 4, 2 or 1 bytes\n");
  {
    image_t rgba(10, 6), r4(10, 6, RGBA4444), pal(10, 6, PAL8, 4), lum(10, 6, LUM8), mask(10, 6, MASK8);
    CHECK(rgba.bytes_per_pixel() == 4 && rgba.buffer_size() == 240 && rgba.row_stride() == 40);
    CHECK(r4.bytes_per_pixel() == 2 && r4.buffer_size() == 120 && r4.row_stride() == 20);
    CHECK(pal.bytes_per_pixel() == 1 && pal.buffer_size() == 60);
    CHECK(lum.bytes_per_pixel() == 1 && lum.buffer_size() == 60 && lum.row_stride() == 10);
    CHECK(mask.bytes_per_pixel() == 1 && mask.buffer_size() == 60);
    CHECK(pal.has_palette() && pal.palette_size() == 4 && !pal.drawable());
    CHECK(!lum.has_palette() && lum.palette_size() == 0 && lum.drawable() && !lum.native());
    CHECK(!mask.has_palette() && !r4.has_palette() && rgba.native());
  }

  printf("formats: expand and pack round trip\n");
  {
    bool lum_ok = true, mask_ok = true;
    for(int v = 0; v < 256; v++) {
      uint8_t b = (uint8_t)v, out = 0;
      uint32_t c = pv_expand_pixel(LUM8, &b);
      lum_ok = lum_ok && c == (0xff000000u | (uint32_t)(v * 0x010101));
      pv_pack_pixel(LUM8, &out, c);
      lum_ok = lum_ok && out == v;
      c = pv_expand_pixel(MASK8, &b);
      mask_ok = mask_ok && c == (uint32_t)v * 0x01010101u;
      pv_pack_pixel(MASK8, &out, c);
      mask_ok = mask_ok && out == v;
    }
    CHECK(lum_ok);
    CHECK(mask_ok);

    bool r4_ok = true;
    for(int v = 0; v < 65536; v++) {
      uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }, out[2] = {0, 0};
      pv_pack_pixel(RGBA4444, out, pv_expand_pixel(RGBA4444, b));
      r4_ok = r4_ok && out[0] == b[0] && out[1] == b[1];
    }
    CHECK(r4_ok);
    uint8_t px[2] = { 0x21, 0x43 };   // r 1, g 2, b 3, a 4
    CHECK(pv_expand_pixel(RGBA4444, px) == 0x44332211u);
    // packs to the nearest level, not the one below
    uint8_t near[2];
    pv_pack_pixel(RGBA4444, near, 0xff0910f7u);
    CHECK(near[0] == 0x1f && near[1] == 0xf1);
  }

  printf("formats: a LUM8 target holds what RGBA8888 would, as luminance\n");
  {
    canvas_t ref(W, H);
    ref.flat(0xff808080u);
    scene(ref.img);

    // guard bytes either side catch a four-byte store into the one-byte rows
    std::vector<uint8_t> buf((size_t)W * H + 64, 0xa5);
    memset(buf.data() + 32, 0x80, (size_t)W * H);
    image_t lum(buf.data() + 32, W, H, LUM8);
    scene(lum);

    std::vector<uint8_t> want((size_t)W * H), got(buf.begin() + 32, buf.end() - 32);
    bool grey = true;
    for(int i = 0; i < W * H; i++) {
      uint32_t c = ref.pixels[i];
      grey = grey && (c & 0xff) == ((c >> 8) & 0xff) && (c & 0xff) == ((c >> 16) & 0xff);
      want[i] = (uint8_t)(c & 0xff);
    }
    CHECK(grey);                              // the reference is what the test assumes
    CHECK(ref.changed_total() > W * H / 4);   // ...and the scene drew something
    CHECK(count_diff(want, got) == 0);
    bool guards = true;
    for(int i = 0; i < 32; i++) guards = guards && buf[i] == 0xa5 && buf[buf.size() - 1 - i] == 0xa5;
    CHECK(guards);
  }

  printf("formats: a LUM8 target takes colour as luminance, and observes clip and alpha\n");
  {
    image_t lum(32, 8, LUM8);
    color_brush_t white(rgb_color_t(255, 255, 255, 255)), red(rgb_color_t(255, 0, 0, 255));
    lum.brush(&white);
    lum.clear();
    CHECK(*(uint8_t *)lum.ptr(0, 0) == 255 && *(uint8_t *)lum.ptr(31, 7) == 255);
    lum.brush(&red);
    lum.clip(rect_t(4, 2, 8, 4));
    lum.rectangle(rect_t(0, 0, 32, 8));
    CHECK(*(uint8_t *)lum.ptr(4, 2) == 76);        // 77 * 255 >> 8
    CHECK(*(uint8_t *)lum.ptr(11, 5) == 76);
    CHECK(*(uint8_t *)lum.ptr(3, 2) == 255 && *(uint8_t *)lum.ptr(12, 5) == 255);
    CHECK(*(uint8_t *)lum.ptr(4, 1) == 255 && *(uint8_t *)lum.ptr(4, 6) == 255);

    color_brush_t black(rgb_color_t(0, 0, 0, 255));
    lum.clip(lum.bounds());
    lum.brush(&white);
    lum.clear();
    lum.brush(&black);
    lum.alpha(128);
    lum.rectangle(rect_t(0, 0, 32, 8));
    lum.alpha(255);
    int v = *(uint8_t *)lum.ptr(16, 4);
    CHECK(v >= 126 && v <= 128);
  }

  printf("formats: a MASK8 target keeps the alpha drawn into it\n");
  {
    canvas_t ref(W, H);
    ref.flat(0);
    scene(ref.img);
    image_t mask(W, H, MASK8);
    memset(mask.ptr(0, 0), 0, mask.buffer_size());
    scene(mask);
    int diff = 0;
    for(int i = 0; i < W * H; i++)
      if(((uint8_t *)mask.ptr(0, 0))[i] != (uint8_t)(ref.pixels[i] >> 24)) diff++;
    CHECK(diff == 0);

    // erasing takes it back out
    transparent_brush_t erase;
    mask.brush(&erase);
    mask.rectangle(rect_t(0, 0, 150, H));
    CHECK(*(uint8_t *)mask.ptr(60, 20) == 0);
    CHECK(*(uint8_t *)mask.ptr(200, 6) == 255);
  }

  printf("formats: an RGBA4444 target holds colour to four bits a channel\n");
  {
    image_t img(W, H, RGBA4444);
    memset(img.ptr(0, 0), 0, img.buffer_size());
    color_brush_t orange(rgb_color_t(255, 136, 0, 255));
    img.brush(&orange);
    img.rectangle(rect_t(0, 0, W, 10));
    CHECK(img.get_unsafe(0, 0) == 0xff0088ffu && img.get_unsafe(W - 1, 9) == 0xff0088ffu);
    CHECK(img.get_unsafe(0, 10) == 0);

    // a blend lands within a level of the RGBA8888 result
    canvas_t ref(W, H);
    ref.flat(0xff808080u);
    scene(ref.img);
    for(int i = 0; i < W * H; i++) pv_pack_pixel(RGBA4444, (uint8_t *)img.ptr(0, 0) + 2 * i, 0xff808080u);
    scene(img);
    int worst = 0;
    for(int y = 0; y < H; y++)
      for(int x = 0; x < W; x++) {
        int d = (int)(img.get_unsafe(x, y) & 0xff) - (int)(ref.at(x, y) & 0xff);
        if(d < 0) d = -d;
        if(d > worst) worst = d;
      }
    // 8 is half a level from rounding the result, and as much again from each
    // of the two blends a pixel takes over an already rounded one
    CHECK(worst <= 24);
  }

  printf("formats: PAL8 is still a source only\n");
  {
    image_t pal(16, 16, PAL8, 4);
    memset(pal.ptr(0, 0), 2, pal.buffer_size());
    color_brush_t white(rgb_color_t(255, 255, 255, 255));
    pal.brush(&white);
    pal.rectangle(rect_t(0, 0, 16, 16));
    pal.antialias(X4);
    pal.shape(circle(8, 8, 6));
    bool untouched = true;
    for(int i = 0; i < 256; i++) untouched = untouched && ((uint8_t *)pal.ptr(0, 0))[i] == 2;
    CHECK(untouched);
  }

  printf("formats: a brush that reads its neighbours leaves a converted target alone\n");
  {
    image_t lum(64, 16, LUM8);
    for(int i = 0; i < 64 * 16; i++) ((uint8_t *)lum.ptr(0, 0))[i] = (uint8_t)(i * 7);
    std::vector<uint8_t> before((uint8_t *)lum.ptr(0, 0), (uint8_t *)lum.ptr(0, 0) + 64 * 16);
    blur_brush_t blur(2);
    pixelate_brush_t pix(4);
    lum.brush(&blur);
    lum.rectangle(rect_t(0, 0, 64, 16));
    lum.brush(&pix);
    lum.rectangle(rect_t(0, 0, 64, 16));
    CHECK(memcmp(before.data(), lum.ptr(0, 0), before.size()) == 0);

    // one that only rewrites its own pixel does draw
    invert_brush_t inv;
    lum.brush(&inv);
    lum.rectangle(rect_t(0, 0, 64, 16));
    bool inverted = true;
    for(size_t i = 0; i < before.size(); i++) inverted = inverted && ((uint8_t *)lum.ptr(0, 0))[i] == 255 - before[i];
    CHECK(inverted);
  }

  // A 300 x 8 RGBA source of greys with a transparent strip
  canvas_t src(W, 8);
  for(int y = 0; y < 8; y++)
    for(int x = 0; x < W; x++) {
      uint32_t g = (uint32_t)((x * 5 + y * 11) & 0xff);
      src.pixels[(size_t)y * W + x] = x >= 100 && x < 120 ? 0 : (0xff000000u | g * 0x010101u);
    }

  printf("formats: blit into LUM8, unscaled and scaled\n");
  {
    image_t lum(W, 16, LUM8);
    memset(lum.ptr(0, 0), 7, lum.buffer_size());
    src.img.blit(&lum, vec2_t(0, 4));
    bool ok = true;
    for(int y = 0; y < 16; y++)
      for(int x = 0; x < W; x++) {
        int want = 7;
        if(y >= 4 && y < 12 && !(x >= 100 && x < 120)) want = (x * 5 + (y - 4) * 11) & 0xff;
        ok = ok && *(uint8_t *)lum.ptr(x, y) == want;
      }
    CHECK(ok);

    // clipped at the target's edges
    memset(lum.ptr(0, 0), 7, lum.buffer_size());
    src.img.blit(&lum, vec2_t(-10, 12));
    CHECK(*(uint8_t *)lum.ptr(0, 12) == ((10 * 5) & 0xff));
    CHECK(*(uint8_t *)lum.ptr(0, 11) == 7);
    CHECK(*(uint8_t *)lum.ptr(W - 1, 15) == 7);   // the source ran out 10 short

    // against the same scaled blit into RGBA8888
    canvas_t ref(200, 20);
    ref.flat(0xff070707u);
    image_t lum2(200, 20, LUM8);
    memset(lum2.ptr(0, 0), 7, lum2.buffer_size());
    for(filter_t f : { NEAREST, BILINEAR }) {
      src.img.blit(&ref.img, rect_t(20, 0, 250, 8), rect_t(3, 2, 190, 15), f);
      src.img.blit(&lum2, rect_t(20, 0, 250, 8), rect_t(3, 2, 190, 15), f);
      int diff = 0;
      for(int i = 0; i < 200 * 20; i++)
        if(((uint8_t *)lum2.ptr(0, 0))[i] != (uint8_t)(ref.pixels[i] & 0xff)) diff++;
      CHECK(diff == 0);
    }
  }

  printf("formats: blit from LUM8, MASK8 and RGBA4444\n");
  {
    image_t lum(16, 4, LUM8), mask(16, 4, MASK8), r4(16, 4, RGBA4444);
    for(int i = 0; i < 64; i++) {
      ((uint8_t *)lum.ptr(0, 0))[i] = (uint8_t)(i * 4);
      ((uint8_t *)mask.ptr(0, 0))[i] = (uint8_t)(i * 4);
      pv_pack_pixel(RGBA4444, (uint8_t *)r4.ptr(0, 0) + 2 * i, 0xff0000ffu);
    }
    canvas_t dst(32, 8);
    dst.flat(0xff000000u);
    lum.blit(&dst.img, vec2_t(2, 1));
    CHECK(dst.at(2, 1) == 0xff000000u && dst.at(3, 1) == 0xff040404u && dst.at(17, 4) == 0xfffcfcfcu);
    CHECK(dst.changed_outside(rect_t(2, 1, 16, 4)) == 0);

    dst.flat(0xff000000u);
    mask.blit(&dst.img, vec2_t(2, 1));
    CHECK(dst.at(2, 1) == 0xff000000u);             // alpha 0: nothing
    CHECK(dst.at(17, 4) == 0xfffcfcfcu);            // white at 252 over black
    dst.flat(0);
    mask.blit(&dst.img, vec2_t(0, 0));
    CHECK(dst.at(8, 0) == 0x20202020u);             // premultiplied white at alpha 32

    dst.flat(0xff000000u);
    r4.blit(&dst.img, vec2_t(0, 0));
    CHECK(dst.at(0, 0) == 0xff0000ffu && dst.at(15, 3) == 0xff0000ffu);
    dst.flat(0xff000000u);
    r4.blit(&dst.img, rect_t(0, 0, 32, 8), BILINEAR);
    CHECK(dst.at(16, 4) == 0xff0000ffu);

    // LUM8 scaled, each filter; BICUBIC falls back to BILINEAR
    for(filter_t f : { NEAREST, BILINEAR, BICUBIC }) {
      dst.flat(0xff000000u);
      lum.blit(&dst.img, rect_t(0, 0, 32, 8), f);
      uint32_t c = dst.at(16, 4);
      CHECK((c >> 24) == 255 && (c & 0xff) == ((c >> 8) & 0xff) && (c & 0xff) > 100 && (c & 0xff) < 200);
    }
  }

  printf("formats: an indexed source blits into LUM8\n");
  {
    image_t pal(8, 8, PAL8, 2);
    pal.palette(0, 0);
    pal.palette(1, rgb_color_t(200, 200, 200, 255)._p);
    for(int i = 0; i < 64; i++) ((uint8_t *)pal.ptr(0, 0))[i] = (uint8_t)((i ^ (i >> 3)) & 1);
    image_t lum(16, 16, LUM8);
    memset(lum.ptr(0, 0), 9, lum.buffer_size());
    pal.blit(&lum, vec2_t(4, 4));
    CHECK(*(uint8_t *)lum.ptr(4, 4) == 9);          // index 0: transparent
    CHECK(*(uint8_t *)lum.ptr(5, 4) == 200);
    CHECK(*(uint8_t *)lum.ptr(3, 4) == 9 && *(uint8_t *)lum.ptr(12, 4) == 9);
    pal.blit(&lum, rect_t(0, 0, 16, 16));
    CHECK(*(uint8_t *)lum.ptr(2, 0) == 200 && *(uint8_t *)lum.ptr(0, 0) == 9);
  }

  printf("formats: interpolated spans land in a LUM8 target\n");
  {
    canvas_t ref(64, 64);
    ref.flat(0xff101010u);
    image_t lum(64, 64, LUM8);
    memset(lum.ptr(0, 0), 0x10, lum.buffer_size());
    for(filter_t f : { NEAREST, BILINEAR }) {
      src.img.blit_hspan(&ref.img, vec2_t(-5, 10), 80, vec2_t(0, 0.5f), vec2_t(1, 0.5f), f);
      src.img.blit_hspan(&lum, vec2_t(-5, 10), 80, vec2_t(0, 0.5f), vec2_t(1, 0.5f), f);
      src.img.blit_vspan(&ref.img, vec2_t(20, -3), 90, vec2_t(0.25f, 0), vec2_t(0.25f, 1), f);
      src.img.blit_vspan(&lum, vec2_t(20, -3), 90, vec2_t(0.25f, 0), vec2_t(0.25f, 1), f);
    }
    int diff = 0;
    for(int i = 0; i < 64 * 64; i++)
      if(((uint8_t *)lum.ptr(0, 0))[i] != (uint8_t)(ref.pixels[i] & 0xff)) diff++;
    CHECK(diff == 0);
    CHECK(ref.changed_total() > 100);
  }

  printf("formats: a view of a LUM8 image draws into its parent's rows\n");
  {
    image_t lum(32, 16, LUM8);
    memset(lum.ptr(0, 0), 0, lum.buffer_size());
    image_t view = lum.window(rect_t(8, 4, 10, 6));
    CHECK(view.row_stride() == 32 && view.pixel_format() == LUM8);
    color_brush_t white(rgb_color_t(255, 255, 255, 255));
    view.brush(&white);
    view.clear();
    int lit = 0;
    for(int i = 0; i < 32 * 16; i++) if(((uint8_t *)lum.ptr(0, 0))[i]) lit++;
    CHECK(lit == 60);
    CHECK(*(uint8_t *)lum.ptr(8, 4) == 255 && *(uint8_t *)lum.ptr(17, 9) == 255);
    CHECK(*(uint8_t *)lum.ptr(7, 4) == 0 && *(uint8_t *)lum.ptr(18, 9) == 0);
  }

  printf("formats: whole-image filters skip a converted image\n");
  {
    image_t lum(32, 32, LUM8);
    for(int i = 0; i < 32 * 32; i++) ((uint8_t *)lum.ptr(0, 0))[i] = (uint8_t)(i * 3);
    std::vector<uint8_t> before((uint8_t *)lum.ptr(0, 0), (uint8_t *)lum.ptr(0, 0) + 32 * 32);
    lum.blur(3.0f);
    lum.bloom(100, 100, 2.0f);
    lum.edgeglow(100);
    lum.wave(4, 4);
    lum.zoom(50);
    CHECK(memcmp(before.data(), lum.ptr(0, 0), before.size()) == 0);
  }
}
