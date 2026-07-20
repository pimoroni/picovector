#include <string.h>
#include <math.h>
#include <algorithm>
#include <vector>

#include "algorithms/algorithms.hpp"
#include "image.hpp"
#include "blend.hpp"
#include "brush.hpp"
#include "shape.hpp"
#include "picovector.hpp"  // pv_parallel_rows (dual-core blit split)
#include "rasteriser.hpp"  // render() / pv_profile_frame()

using std::vector;

namespace picovector {

  // Shared span-accumulation buffer (declared extern in image.hpp). Aligned for
  // pv_masked_span (holds a pointer); reinterpreted per batch as solid/masked.
  alignas(4) uint8_t _span_buf[PV_SPAN_BYTES];
  int _span_n = 0;

  image_t::image_t() {
  }

  image_t::image_t(image_t *source, rect_t r) {
    rect_t i = source->_bounds.intersection(r);
    *this = *source;
    _bounds = rect_t(0, 0, i.w, i.h);
    _clip = rect_t(0, 0, i.w, i.h);
    _buffer = source->ptr(i.x, i.y);
    _managed_buffer = false;
  }

  image_t::image_t(int w, int h, pixel_format_t pixel_format, bool has_palette)
    : image_t(w, h, 1, 1, pixel_format, has_palette) {}

  image_t::image_t(int w, int h, int rows, int cols, pixel_format_t pixel_format, bool has_palette) {
    _bounds = rect_t(0, 0, w, h);
    _clip = rect_t(0, 0, w, h);
    _brush = nullptr;
    _rows = rows;
    _cols = cols;
    _pixel_format = pixel_format;
    _has_palette = has_palette;
    _managed_buffer = true;
    _bytes_per_pixel = this->_has_palette ? sizeof(uint8_t) : sizeof(uint32_t);
    _row_stride = w * _bytes_per_pixel;
    _buffer = PV_MALLOC_NO_SCAN(this->buffer_size());
    if(_has_palette) {
      _palette.resize(256);
    }
  }

  image_t::image_t(void *buffer, int w, int h, pixel_format_t pixel_format, bool has_palette)
    : image_t(buffer, w, h, 1, 1, pixel_format, has_palette) {}

  image_t::image_t(void *buffer, int w, int h, int rows, int cols, pixel_format_t pixel_format, bool has_palette) {
    _bounds = rect_t(0, 0, w, h);
    _clip = rect_t(0, 0, w, h);
    _brush = nullptr;
    _rows = rows;
    _cols = cols;
    _pixel_format = pixel_format;
    _has_palette = has_palette;
    _buffer = buffer;
    _managed_buffer = false;
    _bytes_per_pixel = this->_has_palette ? sizeof(uint8_t) : sizeof(uint32_t);
    _row_stride = w * _bytes_per_pixel;
    if(_has_palette) {
      _palette.resize(256);
    }
  }

  image_t::~image_t() {
    if(this->_managed_buffer) {
#ifdef PICO
      PV_FREE(this->_buffer);
#else
      PV_FREE(this->_buffer, this->buffer_size());
#endif
    }
  }

  size_t image_t::buffer_size() {
    return this->_bytes_per_pixel * this->_bounds.w * this->_bounds.h;
  }

  size_t image_t::bytes_per_pixel() {
    return this->_bytes_per_pixel;
  }

  bool image_t::is_compatible(image_t *other) {
    return this->_palette == other->_palette && this->_pixel_format == other->_pixel_format;
  }

  uint32_t image_t::row_stride() {
    return this->_row_stride;
  }

  rect_t image_t::bounds() {
    return this->_bounds;
  }

  rect_t image_t::clip() {
    return this->_clip;
  }

  void image_t::clip(rect_t r) {
    this->_clip = _bounds.intersection(r);
  }

  bool image_t::has_palette() {
    return this->_has_palette;
  }

  // TODO: why?
  // void image_t::delete_palette() {
  //   if(this->has_palette()) {
  //     this->_palette.clear();
  //   }
  // }

  void image_t::palette(uint8_t i, uint32_t c) {
    this->_palette[i] = c;
  }

  uint32_t image_t::palette(uint8_t i) {
    return this->_palette[i];
  }

  uint8_t image_t::alpha() {
    return this->_alpha;
  }

  void image_t::alpha(uint8_t alpha) {
    // TODO: check if pixel format and palette mode supports alpha
    this->_alpha = alpha;
  }

  antialias_t image_t::antialias() {
    return this->_antialias;
  }

  void image_t::antialias(antialias_t antialias) {
    // TODO: check if pixel format and palette mode supports alpha
    this->_antialias = antialias;
  }

  fill_rule_t image_t::fill_rule() {
    return this->_fill_rule;
  }

  void image_t::fill_rule(fill_rule_t fill_rule) {
    this->_fill_rule = fill_rule;
  }

  uint image_t::rows() {
    return this->_rows;
  }

  void image_t::rows(uint rows) {
    this->_rows = rows;
  }

  uint image_t::cols() {
    return this->_cols;
  }

  void image_t::cols(uint cols) {
    this->_cols = cols;
  }

  pixel_format_t image_t::pixel_format() {
    return this->_pixel_format;
  }

  void image_t::pixel_format(pixel_format_t pixel_format) {
    this->_pixel_format = pixel_format;
  }

  brush_t* image_t::brush() {
    return this->_brush;
  }

  void image_t::brush(brush_t *brush) {
    this->_brush = brush;
  }

  font_t* image_t::font() {
    return this->_font;
  }

  void image_t::font(font_t *font) {
    this->_font = font;
  }

  pixel_font_t* image_t::pixel_font() {
    return this->_pixel_font;
  }

  void image_t::pixel_font(pixel_font_t *pixel_font) {
    this->_pixel_font = pixel_font;
  }

  vec2_t image_t::text_cursor() {
    return vec2_t(this->_text_cursor.x, this->_text_cursor.y);
  }

  void image_t::text_cursor(vec2_t p) {
    this->_text_cursor.x = p.x;
    this->_text_cursor.y = p.y;
    this->_text_cursor.origin_x = p.x;
    this->_text_cursor.valid = true;
  }

  image_t image_t::window(rect_t r) {
    rect_t i = _bounds.intersection(r);
    image_t window = image_t(this, rect_t(i.x, i.y, i.w, i.h));
    return window;
  }

  // Sprite (x, y) in grid coordinates -> a window over that cell. Cell size is
  // the sheet divided by its _cols x _rows layout.
  image_t image_t::sprite(int x, int y) {
    int sw = int(_bounds.w) / int(_cols);
    int sh = int(_bounds.h) / int(_rows);
    return window(rect_t(x * sw, y * sh, sw, sh));
  }

  // void image_t::clear(uint32_t c) {
  //   int count = this->_bounds.w * this->_bounds.h;

  //   printf("clear %p (%d)\n", this->_brush, count);
  //   this->_brush->span_func(this->_brush, 0, 0, count);

  //   // if(_has_palette) {
  //   //   memset(_buffer, c, count);
  //   // }else{
  //   //   int dw8 = count >> 3;   // number of blocks of eight pixels
  //   //   int r = count & 0b111;  // remainder
  //   //   uint32_t* p = (uint32_t*)_buffer;
  //   //   while(dw8--) { // unrolled blocks of 8 pixels
  //   //     *p++ = c; *p++ = c; *p++ = c; *p++ = c;
  //   //     *p++ = c; *p++ = c; *p++ = c; *p++ = c;
  //   //   }
  //   //   while(r--) { // fill in remainder
  //   //     *p++ = c;
  //   //   }
  //   // }
  // }

  void image_t::clear() {
    pv_profile_frame(); // once-per-frame profiling sample (no-op unless PV_PROFILE)
    // Fill the clip with the pen. An opaque colour brush takes its direct-copy
    // fast path in blend_spans, so this is as quick as the old constant-word fill.
    rectangle(_clip);
  }


  void image_t::shape(shape_t *shape) {
    render(shape, this, &shape->transform, _brush);
  }

  // Greyscale filter: fill the image bounds with the monochrome brush (which
  // rewrites each covered pixel to its luminance). The brush lives in
  // brushes/monochrome.cpp.
  void image_t::monochrome() {
    monochrome_brush_t brush;
    brush_t *saved = _brush;
    _brush = &brush;
    rectangle(_bounds);
    _brush = saved;
  }

  // Ordered-dither filter: fill the image bounds with the dither brush.
  void image_t::dither() {
    dither_brush_t brush;
    brush_t *saved = _brush;
    _brush = &brush;
    rectangle(_bounds);
    _brush = saved;
  }

  // 1-bit filter: a luminance threshold at mid-grey to black/white.
  void image_t::onebit() {
    threshold(128, rgb_color_t(0, 0, 0, 255), rgb_color_t(255, 255, 255, 255));
  }

  // The per-pixel colour filters below are each just "fill the image bounds with
  // the matching brush" - the brush (brushes/<name>.cpp) does the work, and the
  // same brush drives brush.<name>() on arbitrary shapes.
  void image_t::invert()              { invert_brush_t b;            brush_t *s = _brush; _brush = &b; rectangle(_bounds); _brush = s; }
  void image_t::threshold(int level, color_t lo, color_t hi) { threshold_brush_t b(level, lo, hi); brush_t *s = _brush; _brush = &b; rectangle(_bounds); _brush = s; }
  void image_t::saturation(int amount){ saturation_brush_t b(amount); brush_t *s = _brush; _brush = &b; rectangle(_bounds); _brush = s; }
  void image_t::contrast(int amount)  { contrast_brush_t b(amount);   brush_t *s = _brush; _brush = &b; rectangle(_bounds); _brush = s; }
  void image_t::duotone(color_t shadow, color_t highlight) { duotone_brush_t b(shadow, highlight); brush_t *s = _brush; _brush = &b; rectangle(_bounds); _brush = s; }
  void image_t::crt(int spacing, int darkness) { crt_brush_t b(spacing, darkness); brush_t *s = _brush; _brush = &b; rectangle(_bounds); _brush = s; }
  void image_t::grid(int spacing, int darkness) { grid_brush_t b(spacing, darkness); brush_t *s = _brush; _brush = &b; rectangle(_bounds); _brush = s; }
  void image_t::vignette(int strength){ vignette_brush_t b(strength); brush_t *s = _brush; _brush = &b; rectangle(_bounds); _brush = s; }
  void image_t::gameboy() {
    auto pk = [](int r, int g, int b) { return (uint32_t)(r | (g << 8) | (b << 16) | (255u << 24)); };
    // DMG olive, warmed toward yellow (raised red, lowered blue)
    uint32_t pal[4] = { pk(40, 44, 8), pk(56, 74, 14), pk(104, 114, 30), pk(182, 172, 60) };
    brush_t *s = _brush;
    palette_dither_brush_t b(pal, 4, 30);
    _brush = &b; rectangle(_bounds);
    grid_brush_t g(2, 22);                            // gentle LCD pixel grid (every other pixel)
    _brush = &g; rectangle(_bounds);
    _brush = s;
  }
  void image_t::noise(int amount, int interval) { noise_brush_t b(amount, interval); brush_t *s = _brush; _brush = &b; rectangle(_bounds); _brush = s; }
  void image_t::glitch(int amount)    { glitch_brush_t b(amount);    brush_t *s = _brush; _brush = &b; rectangle(_bounds); _brush = s; }
  void image_t::oilpaint(int radius, int strength) { oilpaint_brush_t b(radius, strength); brush_t *s = _brush; _brush = &b; rectangle(_bounds); _brush = s; }
  void image_t::cga() {
    auto pk = [](int r, int g, int b) { return (uint32_t)(r | (g << 8) | (b << 16) | (255u << 24)); };
    uint32_t pal[4] = { pk(0, 0, 0), pk(85, 255, 255), pk(255, 85, 255), pk(255, 255, 255) };
    brush_t *s = _brush;
    palette_dither_brush_t b(pal, 4, 16);            // light dither
    _brush = &b; rectangle(_bounds); _brush = s;
    bloom(50, 220, 4);                               // glow first (low threshold so cyan/magenta catch)
    crt_brush_t cb(2, 70);                           // scanlines last, so the bloom can't fill them in
    _brush = &cb; rectangle(_bounds); _brush = s;
  }
  void image_t::palette_dither(const uint32_t *colors, int n, int strength) { palette_dither_brush_t b(colors, n, strength); brush_t *s = _brush; _brush = &b; rectangle(_bounds); _brush = s; }
  void image_t::phosphor(color_t tint) {
    brush_t *s = _brush;
    phosphor_brush_t pb(tint);                       // luminance -> kelly green
    _brush = &pb; rectangle(_bounds); _brush = s;
    crt_brush_t cb(2, 50);                            // scanlines + tube
    _brush = &cb; rectangle(_bounds); _brush = s;
    bloom(35, 420, 7);                                // hot, wide glow
  }
  // Synthwave sunset: dither to a dark-blue / purple / magenta / pale-cyan /
  // warm-white palette at low strength, then bloom for the neon glow.
  void image_t::synthwave() {
    auto pk = [](int r, int g, int b) { return (uint32_t)(r | (g << 8) | (b << 16) | (255u << 24)); };
    uint32_t pal[12] = {
      pk(252, 248, 211), pk(124, 11, 127), pk(70, 13, 110),  pk(249, 7, 153),
      pk(136, 28, 176),  pk(76, 19, 148),  pk(44, 16, 129),  pk(41, 236, 255),
      pk(8, 6, 53),      pk(255, 156, 99), pk(253, 218, 66), pk(253, 250, 93),
    };
    brush_t *s = _brush;
    palette_dither_brush_t b(pal, 12, 20);           // low dither
    _brush = &b; rectangle(_bounds); _brush = s;
    bloom(100, 320, 6);                              // hard glow on the neons
  }
  void image_t::c64() {
    auto pk = [](int r, int g, int b) { return (uint32_t)(r | (g << 8) | (b << 16) | (255u << 24)); };
    uint32_t pal[16] = {
      pk(0, 0, 0),       pk(255, 255, 255), pk(136, 0, 0),     pk(170, 255, 238),
      pk(204, 68, 204),  pk(0, 204, 85),    pk(0, 0, 170),     pk(238, 238, 119),
      pk(221, 136, 85),  pk(102, 68, 0),    pk(255, 119, 119), pk(51, 51, 51),
      pk(119, 119, 119), pk(170, 255, 102), pk(0, 136, 255),   pk(187, 187, 187),
    };
    brush_t *s = _brush;
    saturation_brush_t sat(280);                     // punch saturation so content reaches the vivid C64 colours
    _brush = &sat; rectangle(_bounds);
    palette_dither_brush_t b(pal, 16, 40);
    _brush = &b; rectangle(_bounds); _brush = s;
  }
  void image_t::nightvision()         { nightvision_brush_t b;       brush_t *s = _brush; _brush = &b; rectangle(_bounds); _brush = s; }
  void image_t::chromatic(int offset) { chromatic_brush_t b(offset); brush_t *s = _brush; _brush = &b; rectangle(_bounds); _brush = s; }

  void image_t::rectangle(rect_t r) {
    r = r.intersection(_clip);
    if(r.w <= 0 || r.h <= 0) return;

    // One span per row into the shared buffer, then a single batch blend. A
    // target is never taller than the buffer's ~1365-span capacity, so there's
    // no need to check for overflow.
    _reset_spans();
    for(int y = r.y; y < r.y + r.h; y++) {
      _add_span((int16_t)r.x, (int16_t)y, (uint16_t)r.w);
    }
    _blend_spans(this, this->_brush);
  }

  void image_t::_span(int x, int y, int w) {
    if(y < _clip.y || y >= _clip.y + _clip.h) return;
    if(x + w < _clip.x || x >= _clip.x + _clip.w) return;

    if(x < _clip.x) {
      w -= _clip.x - x;
      x = _clip.x;
    }

    if(x + w >= _clip.x + _clip.w) {
      w = _clip.x + _clip.w - x;
    }
    _add_span(x, y, w);
  }

  void image_t::span(int x, int y, int w) {
    _reset_spans();
    _span(x, y, w);
    _blend_spans(this, this->_brush);
  }

  // hspan and vspan composite through the span buffer + batch blend, like span()/
  // rectangle(): hspan is one wide span; vspan is a column of `h` 1px spans. Both
  // honour _clip and the current pen. (vspan's `h` must fit the span buffer's
  // ~1365-span capacity, the same assumption any tall fill makes.)
  void image_t::hspan(int x, int y, int w) {
    _reset_spans();
    _span(x, y, w); // clips against _clip and adds one span to the shared buffer
    _blend_spans(this, _brush);
  }

  void image_t::vspan(int x, int y, int h) {
    if(x < _clip.x || x >= _clip.x + _clip.w) return;
    if(y + h < _clip.y || y >= _clip.y + _clip.h) return;
    if(y < _clip.y) { h -= _clip.y - y; y = _clip.y; }
    if(y + h >= _clip.y + _clip.h) { h = _clip.y + _clip.h - y; }
    if(h <= 0) return;
    _reset_spans();
    for(int i = 0; i < h; i++) _add_span(x, y + i, 1);
    _blend_spans(this, _brush);
  }

  void image_t::masked_span(int x, int y, int w, uint8_t *mask) {
    if(y < _clip.y || y >= _clip.y + _clip.h) return;
    if(x + w < _clip.x || x >= _clip.x + _clip.w) return;

    if(x < _clip.x) {
      w -= _clip.x - x;
      x = _clip.x;
    }

    if(x + w >= _clip.x + _clip.w) {
      w = _clip.x + _clip.w - x;
    }

    _reset_spans();
    _add_masked_span(x, y, w, mask);
    _blend_masked_spans(this, this->_brush);
  }

  void image_t::circle(const vec2_t &p, const int &r) {
    rect_t b = rect_t(p.x - r, p.y - r, r * 2, r * 2);
    if(!b.intersects(_clip)) return;

    // Accumulate every scanline span of the circle, then blend once. Radius is
    // bounded by the target height, so the span buffer can't overflow.
    _reset_spans();
    int ox = r, oy = 0, err = -r;
    while (ox >= oy)
    {
      int last_oy = oy;

      err += oy; oy++; err += oy;

      this->_span(p.x - ox, p.y + last_oy, ox * 2 + 1);
      if (last_oy != 0) {
        this->_span(p.x - ox, p.y - last_oy, ox * 2 + 1);
      }

      if(err >= 0 && ox != last_oy) {
        this->_span(p.x - last_oy, p.y + ox, last_oy * 2 + 1);
        if (ox != 0) {
          this->_span(p.x - last_oy, p.y - ox, last_oy * 2 + 1);
        }

        err -= ox; ox--; err -= ox;
      }
    }
    _blend_spans(this, this->_brush);
  }

  int32_t orient2d(vec2_t p1, vec2_t p2, vec2_t p3) {
    return (p2.x - p1.x) * (p3.y - p1.y) - (p2.y - p1.y) * (p3.x - p1.x);
  }

  bool is_top_left(const vec2_t &p1, const vec2_t &p2) {
    return (p1.y == p2.y && p1.x > p2.x) || (p1.y < p2.y);
  }

  void image_t::triangle(vec2_t p1, vec2_t p2, vec2_t p3) {
    rect_t b(
      vec2_t(min(p1.x, min(p2.x, p3.x)), min(p1.y, min(p2.y, p3.y))),
      vec2_t(max(p1.x, max(p2.x, p3.x)), max(p1.y, max(p2.y, p3.y)))
    );

    // clip extremes to frame buffer size
    b = b.intersection(_clip);

    // if triangle completely out of bounds then don't bother!
    if (b.empty()) return;

    // fix "winding" of vertices if needed
    int32_t winding = orient2d(p1, p2, p3);
    if (winding < 0) {
      vec2_t t;
      t = p1; p1 = p3; p3 = t;
    }

    // bias ensures no overdraw between neighbouring triangles
    int8_t bias0 = is_top_left(p2, p3) ? 0 : -1;
    int8_t bias1 = is_top_left(p3, p1) ? 0 : -1;
    int8_t bias2 = is_top_left(p1, p2) ? 0 : -1;

    int32_t a01 = p1.y - p2.y;
    int32_t b01 = p2.x - p1.x;
    int32_t a12 = p2.y - p3.y;
    int32_t b12 = p3.x - p2.x;
    int32_t a20 = p3.y - p1.y;
    int32_t b20 = p1.x - p3.x;

    vec2_t tl(b.x, b.y);
    int32_t w0row = orient2d(p2, p3, tl) + bias0;
    int32_t w1row = orient2d(p3, p1, tl) + bias1;
    int32_t w2row = orient2d(p1, p2, tl) + bias2;

    _reset_spans();
    for (int32_t y = 0; y < b.h; y++) {
      int32_t w0 = w0row;
      int32_t w1 = w1row;
      int32_t w2 = w2row;

      int xo = b.x;
      int yo = b.y + y;
      int run = -1; // start x of the current covered run (-1 = none)
      for (int32_t x = 0; x < b.w; x++) {
        if ((w0 | w1 | w2) >= 0) {
          if (run < 0) run = xo;               // coalesce contiguous covered pixels
        } else if (run >= 0) {
          _add_span(run, yo, xo - run);
          run = -1;
        }

        xo++;
        w0 += a12; w1 += a20; w2 += a01;
      }
      if (run >= 0) _add_span(run, yo, xo - run); // run reaches the row edge

      w0row += b12; w1row += b20; w2row += b01;

    }
    _blend_spans(this, this->_brush);
  }

  void round_rectangle(const rect_t &r, int radius) {

  }


  void ellipse(const vec2_t &p, const int &rx, const int &ry) {

  }


  void image_t::line(vec2_t p1, vec2_t p2) {
    rect_t b = this->_clip;
    b.w -= 1;
    b.h -= 1; // TODO: this is hacky... fix it properly
    if(!clip_line(p1, p2, b)) {
      return; // fully outside bounds, nothing to draw
    }

    int x0 = p1.x;
    int x1 = p2.x;
    int y0 = p1.y;
    int y1 = p2.y;

    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    _reset_spans();
    while(true) {
        _add_span(x0, y0, 1);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {err += dy; x0 += sx;}
        if (e2 <= dx) {err += dx; y0 += sy;}
    }
    _blend_spans(this, this->_brush);
  }

  void image_t::put(const vec2_t &p) {
    this->put(p.x, p.y);
  }

  void image_t::put(int x, int y) {
    if(x < _clip.x || x >= _clip.x + _clip.w || y < _clip.y || y >= _clip.y + _clip.h) {
      return;
    }
    put_unsafe(x, y);
  }

  void image_t::put_unsafe(int x, int y) {
    _reset_spans();
    _add_span(x, y, 1);
    _blend_spans(this, this->_brush);
  }

  uint32_t image_t::get(const vec2_t &p) {
    return this->get(p.x, p.y);
  }

  uint32_t image_t::get(int x, int y) {
    x = max(int(_clip.x), min(x, int(_clip.x + _clip.w - 1)));
    y = max(int(_clip.y), min(y, int(_clip.y + _clip.h - 1)));
    return this->get_unsafe(x, y);
  }

}