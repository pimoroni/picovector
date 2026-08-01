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

namespace pvtest {

  using namespace picovector;

  // A canvas whose pixels are all distinct, so a filter that only rewrites what
  // it reads still shows up as a change.
  struct canvas_t {
    int w, h;
    std::vector<uint32_t> pixels, before;
    image_t img;

    canvas_t(int w = 64, int h = 64)
      : w(w), h(h), pixels((size_t)w * h), before((size_t)w * h),
        img(pixels.data(), w, h) {
      for(int i = 0; i < w * h; i++)
        pixels[i] = 0xff000000u | (uint32_t)((i * 37) & 0xff)
                  | ((uint32_t)((i * 91) & 0xff) << 8)
                  | ((uint32_t)((i * 13) & 0xff) << 16);
      snapshot();
    }

    void flat(uint32_t c) { for(auto &p : pixels) p = c; snapshot(); }
    void snapshot() { before = pixels; }

    uint32_t at(int x, int y) const { return pixels[(size_t)y * w + x]; }
    bool changed(int x, int y) const { return pixels[(size_t)y * w + x] != before[(size_t)y * w + x]; }

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
      for(int i = 0; i < w * h; i++) if(pixels[i] != before[i]) n++;
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
