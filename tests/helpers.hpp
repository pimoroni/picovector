// Shared fixtures for the host tests: a canvas with a known background, and the
// probes used to ask what a drawing operation actually wrote.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "picovector.hpp"
#include "image.hpp"
#include "shape.hpp"
#include "primitive.hpp"
#include "brush.hpp"
#include "rasteriser.hpp"
#include "pixel_store.hpp"

namespace pvtest {

  using namespace picovector;

  // The colour this canvas can actually hold: the identity at RGBA8888,
  // pack-then-expand at a narrower format. An assertion written as
  // `at(x, y) == quant(expected)` reads as "the nearest colour this canvas can
  // hold" and is unchanged in the RGBA8888 run.
  inline uint32_t quant(uint32_t c) { return pv_unpack(pv_pack(c)); }

  // How far a channel read back can sit from the value that was blended: nothing
  // where a channel is stored whole, half a quantisation step where it is not.
  // Widening a window by this leaves the RGBA8888 assertion exactly as tight.
#if PV_PIXEL_FORMAT == PV_PIXEL_RGBA4444
  static const int quant_tol = 9;
#else
  static const int quant_tol = 0;
#endif

  // A canvas whose pixels are all distinct, so a filter that only rewrites what
  // it reads still shows up as a change.
  //
  // Backed by bytes, not words, because a stored pixel is not always four bytes.
  // The probes read through the image so a test written once asks the same
  // question of any format: at() hands back an expanded pixel, and changed()
  // compares the bytes actually stored, so a difference too small to survive
  // quantisation correctly does not read as a change.
  struct canvas_t {
    int w, h;
    std::vector<uint8_t> pixels, before;
    image_t img;

    canvas_t(int w = 64, int h = 64)
      : w(w), h(h),
        pixels((size_t)w * h * sizeof(pv_store_t)),
        before((size_t)w * h * sizeof(pv_store_t)),
        img(pixels.data(), w, h) {
      for(int i = 0; i < w * h; i++)
        store(i, 0xff000000u | (uint32_t)((i * 37) & 0xff)
                | ((uint32_t)((i * 91) & 0xff) << 8)
                | ((uint32_t)((i * 13) & 0xff) << 16));
      snapshot();
    }

    size_t bpp() const { return sizeof(pv_store_t); }
    void store(int i, uint32_t c) { pv_store((pv_store_t *)pixels.data() + i, c); }

    void flat(uint32_t c) { for(int i = 0; i < w * h; i++) store(i, c); snapshot(); }
    void snapshot() { before = pixels; }

    uint32_t at(int x, int y) const { return img.get_unsafe(x, y); }

    bool changed(int x, int y) const {
      size_t off = ((size_t)y * w + x) * sizeof(pv_store_t);
      return memcmp(&pixels[off], &before[off], sizeof(pv_store_t)) != 0;
    }

    // pixels written outside `r`
    int changed_outside(rect_t r) const {
      int n = 0;
      for(int y = 0; y < h; y++)
        for(int x = 0; x < w; x++) {
          bool in = x >= (int)r.x && x < (int)(r.x + r.w) &&
                    y >= (int)r.y && y < (int)(r.y + r.h);
          if(!in && changed(x, y)) n++;
        }
      return n;
    }

    int changed_total() const {
      int n = 0;
      for(int y = 0; y < h; y++)
        for(int x = 0; x < w; x++) if(changed(x, y)) n++;
      return n;
    }
  };

  // Ink counts against a black background drawn in white: `full` is solid
  // coverage, `inked` includes the antialiased fringe.
  struct ink_t { int inked, full, partial, minx, miny, maxx, maxy; };

  inline ink_t measure_ink(const canvas_t &c) {
    ink_t k{0, 0, 0, c.w, c.h, -1, -1};
    for(int y = 0; y < c.h; y++)
      for(int x = 0; x < c.w; x++) {
        uint32_t v = c.at(x, y) & 0xff;
        if(!v) continue;
        k.inked++;
        if(v == 255) k.full++;
        if(x < k.minx) k.minx = x;
        if(x > k.maxx) k.maxx = x;
        if(y < k.miny) k.miny = y;
        if(y > k.maxy) k.maxy = y;
      }
    k.partial = k.inked - k.full;
    return k;
  }

  // A black canvas with a white pen, ready to draw one shape into.
  struct ink_canvas_t : canvas_t {
    rgb_color_t pen;
    color_brush_t brush;
    ink_canvas_t(int w = 64, int h = 64, antialias_t aa = X4)
      : canvas_t(w, h), pen(255, 255, 255, 255), brush(pen) {
      flat(0xff000000u);
      img.antialias(aa);
      img.brush(&brush);
    }
    void draw(shape_t *s) { mat3_t t; render(s, &img, &t, &brush); }
  };

}
