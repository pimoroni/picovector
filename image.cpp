#include <string.h>
#include <math.h>
#include <algorithm>
#include <vector>

#include "rasteriser.hpp"
#include "algorithms/algorithms.hpp"
#include "image.hpp"
#include "blend.hpp"
#include "blit.hpp"
#include "brush.hpp"
#include "shape.hpp"
#include "picovector.hpp" // pv_parallel_rows (dual-core blit split)

using std::vector;

namespace picovector {

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

  image_t::image_t(int w, int h, pixel_format_t pixel_format, bool has_palette) {
    _bounds = rect_t(0, 0, w, h);
    _clip = rect_t(0, 0, w, h);
    _brush = nullptr;
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

  image_t::image_t(void *buffer, int w, int h, pixel_format_t pixel_format, bool has_palette) {
    _bounds = rect_t(0, 0, w, h);
    _clip = rect_t(0, 0, w, h);
    _brush = nullptr;
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
    this->_span_func = brush->span_func();
    this->_masked_span_func = brush->masked_span_func();
    // this->_span_func = brush->get_span_func(this);
    // this->_mask_span_func = brush->get_mask_span_func(this);
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

  image_t image_t::window(rect_t r) {
    rect_t i = _bounds.intersection(r);
    image_t window = image_t(this, rect_t(i.x, i.y, i.w, i.h));
    return window;
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

    // Fast path: an opaque solid-colour brush on a 32bpp target fills directly
    // with a constant word, skipping the per-pixel blend in the span function.
    uint32_t word;
    if(_brush && _bytes_per_pixel == 4 && _brush->solid_fill(word)) {
      int x0 = (int)_clip.x, y0 = (int)_clip.y;
      int w = (int)_clip.w, h = (int)_clip.h;
      for(int y = 0; y < h; y++) {
        uint32_t *p = (uint32_t *)ptr(x0, y0 + y);
        for(int i = 0; i < w; i++) p[i] = word;
      }
      return;
    }

    rectangle(_clip);
  }


  void clip_blit_rect(rect_t &r1, rect_t b, rect_t &r2) {
    // perform any source rect clipping that's needed
    float sx = r2.w / r1.w;
    float sy = r2.h / r1.h;

    if(r1.x < b.x) {
      float d = b.x - r1.x;
      r1.x += d;
      r1.w -= d;
      r2.x += (d * sx);
      r2.w -= (d * sx);
    }

    if(r1.y < b.y) {
      float d = b.y - r1.y;
      r1.y += d;
      r1.h -= d;
      r2.y += (d * sy);
      r2.h -= (d * sy);
    }

    if(r1.x + r1.w > b.x + b.w) {
      float d = (r1.x + r1.w) - (b.x + b.w);
      r1.w -= d;
      r2.w -= (d * sx);
    }

    if(r1.y + r1.h > b.y + b.h) {
      float d = (r1.y + r1.h) - (b.y + b.h);
      r1.h -= d;
      r2.h -= (d * sy);
    }
  }

#if PV_DUAL_CORE
  namespace {
    // one-to-one (unscaled) blit, a disjoint row parity per core
    struct pv_blit_ctx {
      image_t *src; image_t *dst; blend_func_t bf;
      int srx, sry, trx, try_, trw;
      bool has_palette; const palette_t *palette;
    };
    void pv_blit_rows(void *v, int y0, int y1, int step) {
      pv_blit_ctx *c = (pv_blit_ctx *)v;
      if(c->has_palette) {
        for(int y = y0; y < y1; y += step)
          span_blit(c->src, c->dst, c->bf, c->srx, c->sry + y, c->trx, c->try_ + y, c->trw, *c->palette);
      } else {
        for(int y = y0; y < y1; y += step)
          span_blit(c->src, c->dst, c->bf, c->srx, c->sry + y, c->trx, c->try_ + y, c->trw);
      }
    }

    // scaled/filtered blit. srcy is linear in the row index, so each core derives
    // its own srcy (srcy0 + y*srcstepy) rather than sharing a running accumulator.
    struct pv_blit_scale_ctx {
      image_t *src; image_t *dst; blend_func_t bf;
      int srcx, srcstepx, srcy0, srcstepy, trx, try_, trw;
      filter_t filter;
    };
    void pv_blit_scale_rows(void *v, int y0, int y1, int step) {
      pv_blit_scale_ctx *c = (pv_blit_scale_ctx *)v;
      for(int y = y0; y < y1; y += step) {
        int srcy = c->srcy0 + y * c->srcstepy;
        span_blit_scale(c->src, c->dst, c->bf, c->srcx, c->srcstepx, srcy, c->trx, c->try_ + y, c->trw, c->filter);
      }
    }
  }
#endif

  void image_t::blit(image_t *target, const vec2_t p) {
    rect_t sr = _bounds;
    sr = sr.floor();

    rect_t tr(floorf(p.x), floorf(p.y), sr.w, sr.h); // target rect

    clip_blit_rect(sr, _bounds, tr);
    clip_blit_rect(tr, target->_bounds, sr);
    if(sr.w <= 0 || sr.h <= 0 || tr.w <= 0 || tr.h <= 0) {
      return;
    }

    blend_func_t bf = target->_blend_func;

#if PV_DUAL_CORE
    // large enough to amortise the inter-core handshake: split rows across cores
    if((int)tr.w * (int)tr.h >= PV_DUAL_CORE_BLIT_MIN_PX) {
      pv_blit_ctx ctx{ this, target, bf, (int)sr.x, (int)sr.y, (int)tr.x, (int)tr.y, (int)tr.w,
                       _has_palette, &_palette };
      pv_parallel_rows(pv_blit_rows, &ctx, 0, (int)tr.h);
      return;
    }
#endif

    // _has_palette is fixed for the whole blit, so pick the span variant once
    // rather than re-testing it every scanline.
    if(_has_palette) {
      for(int y = 0; y < tr.h; y++) {
        span_blit(this, target, bf, sr.x, sr.y + y, tr.x, tr.y + y, tr.w, _palette);
      }
    } else {
      for(int y = 0; y < tr.h; y++) {
        span_blit(this, target, bf, sr.x, sr.y + y, tr.x, tr.y + y, tr.w);
      }
    }
  }


  // blit from source rectangle into target rectangle
  void image_t::blit(image_t *target, rect_t sr, rect_t tr, filter_t filter) {
    bool flip_h = tr.w < 0;
    bool flip_v = tr.h < 0;

    // clip target rect to target bounds
    // printf("pre clip\n");
    // printf("- sr = %.2f, %.2f (%.2f x %.2f)\n", sr.x, sr.y, sr.w, sr.h);
    // printf("- tr = %.2f, %.2f (%.2f x %.2f)\n", tr.x, tr.y, tr.w, tr.h);
    tr.w = fabs(tr.w);
    tr.h = fabs(tr.h);
    // keep the source rect fractional so sub-texel pan/zoom stays smooth under
    // BILINEAR/BICUBIC (rounding it here snaps the sampled region to whole
    // texels, which steps visibly when magnifying a small source). The dest
    // rect is still snapped to whole pixels — that's the integer span loop.
    tr = tr.round();
    clip_blit_rect(sr, _bounds, tr);
    clip_blit_rect(tr, target->_bounds, sr);
    if(sr.w <= 0 || sr.h <= 0 || tr.w <= 0 || tr.h <= 0) {
      return;
    }
    // printf("post clip\n");
    // printf("- sr = %.2f, %.2f (%.2f x %.2f)\n", sr.x, sr.y, sr.w, sr.h);
    // printf("- tr = %.2f, %.2f (%.2f x %.2f)\n", tr.x, tr.y, tr.w, tr.h);

    blend_func_t bf = target->_blend_func;

    // render the scaled spans
    int srcstepx = (sr.w / tr.w) * 65536.0f;
    int srcstepy = (sr.h / tr.h) * 65536.0f;

    int srcx = sr.x * 65536.0f;
    int srcy = sr.y * 65536.0f;

    if(flip_h) {
      srcstepx = -srcstepx;
      srcx = ((_bounds.w - sr.x) * 65536.0f) + srcstepx;
    }

    if(flip_v) {
      srcstepy = -srcstepy;
      srcy = ((_bounds.h - sr.y) * 65536.0f) + srcstepy;
    }

#if PV_DUAL_CORE
    // scaled/filtered blits are the biggest dual-core win (arithmetic-bound, so
    // closer to a true 2x than a bandwidth-bound copy). Split rows once large.
    if((int)tr.w * (int)tr.h >= PV_DUAL_CORE_BLIT_MIN_PX) {
      pv_blit_scale_ctx ctx{ this, target, bf, srcx, srcstepx, srcy, srcstepy,
                             (int)tr.x, (int)tr.y, (int)tr.w, filter };
      pv_parallel_rows(pv_blit_scale_rows, &ctx, 0, (int)tr.h);
      return;
    }
#endif

    // span_blit_scale resolves palette vs rgba internally via src->has_palette()
    // (the palette overload just forwards), so no per-row dispatch is needed here.
    for(int y = tr.y; y < tr.y + tr.h; y++) {
      span_blit_scale(this, target, bf, srcx, srcstepx, srcy, tr.x, y, tr.w, filter);
      srcy += srcstepy;
    }
  }


  void image_t::blit(image_t *target, rect_t tr, filter_t filter) {
    blit(target, _bounds, tr, filter);
  }

  /*
    blits a span of pixels onto the target image using interpolated samples from
    the source image along a line starting at uv0 and ending at uv1. `vertical`
    picks the travel axis: horizontal (stepping one pixel across a row) or
    vertical (stepping one row down a column). Shared by blit_hspan/blit_vspan.
  */
  void image_t::blit_span(image_t *target, vec2_t p, int c, vec2_t uv0, vec2_t uv1, filter_t filter, bool vertical) {
    rect_t b = target->_clip;
    if(p.x < b.x || p.x > b.x + b.w) {
      return;
    }

    fx16_t u = f_to_fx16(uv0.x);
    fx16_t v = f_to_fx16(uv0.y);

    fx16_t ud = f_to_fx16(uv1.x - uv0.x) / c;
    fx16_t vd = f_to_fx16(uv1.y - uv0.y) / c;

    // clip against the travel axis (y for a vertical span, x for a horizontal one)
    float pp = vertical ? p.y : p.x;
    float bo = vertical ? b.y : b.x;
    float bs = vertical ? b.h : b.w;

    if(pp < bo) {
      u += ud * (bo - pp);
      v += vd * (bo - pp);
      c -= int(bo - pp);
      pp = bo;
    }

    if(pp + c > bo + bs) {
      c = bs - pp;
    }

    if(vertical) p.y = pp; else p.x = pp;

    uint32_t tw = int(this->_bounds.w - 1);
    uint32_t th = int(this->_bounds.h - 1);

    // one pixel across a row, or one row (stride) down a column
    int dst_step = vertical ? (target->_row_stride >> 2) : 1;

    uint32_t *dst = (uint32_t *)target->ptr(p.x, p.y);
    for(int i = 0; i < c; i++) {
      u += ud;
      v += vd;

      // fixed-point 16.16 source coordinates (fractional uv scaled to texels)
      fx16_t sx = (fx16_t)((u & 0xffffu) * tw);
      fx16_t sy = (fx16_t)((v & 0xffffu) * th);
      uint32_t col = this->sample(sx, sy, filter);

      if(this->_alpha != 255) {
        col = _premul_mul_alpha(col, this->_alpha);
      }

      *dst = blend_over_premul(*dst, col);
      dst += dst_step;
    }
  }

  void image_t::blit_hspan(image_t *target, vec2_t p, int c, vec2_t uv0, vec2_t uv1, filter_t filter) {
    blit_span(target, p, c, uv0, uv1, filter, false);
  }

  void image_t::blit_vspan(image_t *target, vec2_t p, int c, vec2_t uv0, vec2_t uv1, filter_t filter) {
    blit_span(target, p, c, uv0, uv1, filter, true);
  }


  void image_t::shape(shape_t *shape) {
    render(shape, this, &shape->transform, _brush);
  }

  void image_t::rectangle(rect_t r) {
    r = r.intersection(_clip);
    span_func_t fn = this->_span_func;
    for(int y = r.y; y < r.y + r.h; y++) {
      fn(this, this->_brush, r.x, y, r.w);
    }
  }

  void image_t::span(int x, int y, int w) {
    if(y < _clip.y || y >= _clip.y + _clip.h) return;
    if(x + w < _clip.x || x >= _clip.x + _clip.w) return;

    if(x < _clip.x) {
      w -= _clip.x - x;
      x = _clip.x;
    }

    if(x + w >= _clip.x + _clip.w) {
      w = _clip.x + _clip.w - x;
    }
    this->_span_func(this, this->_brush, x, y, w);
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

    this->_masked_span_func(this, this->_brush, x, y, w, mask);
  }

  void image_t::circle(const vec2_t &p, const int &r) {
    rect_t b = rect_t(p.x - r, p.y - r, r * 2, r * 2);
    if(!b.intersects(_clip)) return;

    int ox = r, oy = 0, err = -r;
    while (ox >= oy)
    {
      int last_oy = oy;

      err += oy; oy++; err += oy;

      this->span(p.x - ox, p.y + last_oy, ox * 2 + 1);
      if (last_oy != 0) {
        this->span(p.x - ox, p.y - last_oy, ox * 2 + 1);
      }

      if(err >= 0 && ox != last_oy) {
        this->span(p.x - last_oy, p.y + ox, last_oy * 2 + 1);
        if (ox != 0) {
          this->span(p.x - last_oy, p.y - ox, last_oy * 2 + 1);
        }

        err -= ox; ox--; err -= ox;
      }
    }
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

    span_func_t fn = this->_span_func;

    for (int32_t y = 0; y < b.h; y++) {
      int32_t w0 = w0row;
      int32_t w1 = w1row;
      int32_t w2 = w2row;

      int xo = b.x;
      int yo = b.y + y;
      for (int32_t x = 0; x < b.w; x++) {
        if ((w0 | w1 | w2) >= 0) {
          fn(this, this->_brush, xo, yo, 1);
        }

        xo++;
        w0 += a12; w1 += a20; w2 += a01;
      }

      w0row += b12; w1row += b20; w2row += b01;

    }
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

    span_func_t fn = this->_span_func;

    while(true) {
        fn(this, this->_brush, x0, y0, 1);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {err += dy; x0 += sx;}
        if (e2 <= dx) {err += dx; y0 += sy;}
    }
  }

  void image_t::put(const vec2_t &p) {
    this->put(p.x, p.y);
  }

  void image_t::put(int x, int y) {
    if(x < _clip.x || x >= _clip.x + _clip.w || y < _clip.y || y >= _clip.y + _clip.h) {
      return;
    }
    this->_span_func(this, this->_brush, x, y, 1);
  }

  void image_t::put_unsafe(int x, int y) {
    this->_span_func(this, this->_brush, x, y, 1);
    //this->_brush->render_span(this, x, y, 1);
  }

  uint32_t image_t::get(const vec2_t &p) {
    return this->get(p.x, p.y);
  }

  uint32_t image_t::get(int x, int y) {
    x = max(int(_clip.x), min(x, int(_clip.x + _clip.w - 1)));
    y = max(int(_clip.y), min(y, int(_clip.y + _clip.h - 1)));
    return this->get_unsafe(x, y);
  }

  // Catmull-Rom (a = -0.5) cubic convolution weights for the four taps either
  // side of a sample point at fractional offset t. Fixed-point: t and the
  // returned weights are Q12; the weights sum to 1.0 (4096). Integer MACs are
  // cheaper than float on the M33 and skip the per-channel int<->float casts.
  static inline void _cubic_weights_fx(int t, int w[4]) {
    int t2 = (t * t) >> 12;
    int t3 = (t2 * t) >> 12;
    w[0] = (-t3 + 2 * t2 - t) >> 1;
    w[1] = (3 * t3 - 5 * t2 + 8192) >> 1;  // 8192 == 2.0 in Q12
    w[2] = (-3 * t3 + 4 * t2 + t) >> 1;
    w[3] = (t3 - t2) >> 1;
  }

  uint32_t image_t::sample(fx16_t sx, fx16_t sy, filter_t filter) {
    int w = (int)this->_bounds.w;
    int h = (int)this->_bounds.h;
    int ix = sx >> 16;
    int iy = sy >> 16;

    // palette indices can't be interpolated, so always sample nearest for them
    if(filter == NEAREST || this->_has_palette) {
      if(ix < 0) ix = 0; else if(ix >= w) ix = w - 1;
      if(iy < 0) iy = 0; else if(iy >= h) iy = h - 1;
      return get_unsafe(ix, iy);
    }

    if(filter == BILINEAR) {
      uint32_t fx = (sx >> 8) & 0xffu;  // sub-texel fraction 0..255
      uint32_t fy = (sy >> 8) & 0xffu;
      int x0 = ix, x1 = ix + 1;
      int y0 = iy, y1 = iy + 1;
      if(x0 < 0) x0 = 0; else if(x0 >= w) x0 = w - 1;
      if(x1 < 0) x1 = 0; else if(x1 >= w) x1 = w - 1;
      if(y0 < 0) y0 = 0; else if(y0 >= h) y0 = h - 1;
      if(y1 < 0) y1 = 0; else if(y1 >= h) y1 = h - 1;
      uint32_t c00 = get_unsafe(x0, y0), c10 = get_unsafe(x1, y0);
      uint32_t c01 = get_unsafe(x0, y1), c11 = get_unsafe(x1, y1);
      uint32_t out = 0;
      for(int s = 0; s < 32; s += 8) {
        int a = (c00 >> s) & 0xff, b = (c10 >> s) & 0xff;
        int c = (c01 >> s) & 0xff, d = (c11 >> s) & 0xff;
        int top = a + (((b - a) * (int)fx) >> 8);
        int bot = c + (((d - c) * (int)fx) >> 8);
        int val = top + (((bot - top) * (int)fy) >> 8);
        out |= ((uint32_t)val) << s;
      }
      return out;
    }

    // BICUBIC: separable 4x4 Catmull-Rom over premultiplied channels. Only
    // reached for non-palette images, so we read framebuffer words directly via
    // per-row pointers (skips get_unsafe's palette branch + per-texel address
    // maths) and accumulate in fixed-point.
    int wx[4], wy[4];
    _cubic_weights_fx((sx >> 4) & 0xfff, wx);  // sx/sy fraction (Q16) -> Q12
    _cubic_weights_fx((sy >> 4) & 0xfff, wy);

    // clamp the four source columns once; reused for every row
    int xs[4];
    for(int k = 0; k < 4; k++) {
      int x = ix - 1 + k;
      xs[k] = x < 0 ? 0 : (x >= w ? w - 1 : x);
    }

    int ar = 0, ag = 0, ab = 0, aa = 0;  // vertical accumulators, Q18
    for(int j = 0; j < 4; j++) {
      int y = iy - 1 + j;
      if(y < 0) y = 0; else if(y >= h) y = h - 1;
      const uint32_t *row = (const uint32_t *)ptr(0, y);

      int hr = 0, hg = 0, hb = 0, ha = 0;  // horizontal sums, Q12
      for(int i = 0; i < 4; i++) {
        uint32_t c = row[xs[i]];
        int wi = wx[i];
        hr += wi * (int)(c & 0xff);
        hg += wi * (int)((c >> 8) & 0xff);
        hb += wi * (int)((c >> 16) & 0xff);
        ha += wi * (int)((c >> 24) & 0xff);
      }
      // >>6 drops Q12->Q6 so the Q12 vertical weight can't overflow int32
      int wj = wy[j];
      ar += wj * (hr >> 6);
      ag += wj * (hg >> 6);
      ab += wj * (hb >> 6);
      aa += wj * (ha >> 6);
    }

    // accumulators are Q18 (Q12 vertical * Q6 horizontal); round and clamp
    int rr = (ar + (1 << 17)) >> 18;
    int gg = (ag + (1 << 17)) >> 18;
    int bb = (ab + (1 << 17)) >> 18;
    int av = (aa + (1 << 17)) >> 18;
    if(rr < 0) rr = 0; else if(rr > 255) rr = 255;
    if(gg < 0) gg = 0; else if(gg > 255) gg = 255;
    if(bb < 0) bb = 0; else if(bb > 255) bb = 255;
    if(av < 0) av = 0; else if(av > 255) av = 255;
    return (uint32_t)rr | ((uint32_t)gg << 8) | ((uint32_t)bb << 16) | ((uint32_t)av << 24);
  }
}