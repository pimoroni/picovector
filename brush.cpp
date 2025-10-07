#include "brush.hpp"
#include "span.hpp"

using namespace std;

namespace picovector {

  #define debug_printf(fmt, ...) fprintf(stdout, fmt, ##__VA_ARGS__)

  void brush::render_mask(image *target) {
    for(int y = 0; y < target->bounds.h; y++) {
      this->render_span(target, 0, y, target->bounds.w);
    }
  }

  void brush::render_spans(image *target, _rspan *spans, int count) {
    while(count--) {  
      this->render_span(target, spans->x, spans->y, spans->w);
      spans++;
    }
  }

  color_brush::color_brush(int r, int g, int b, int a) {
    this->color = _make_col(r, g, b, a);
  }

  void color_brush::render_span(image *target, int x, int y, int w) {
    uint32_t *dst = target->ptr(x, y);
    span_argb8(dst, w, color);    
  }

  void color_brush::render_span_buffer(image *target, int x, int y, int w, uint8_t *sb) {
    uint32_t *dst = target->ptr(x, y);
    span_argb8(dst, w, color, sb);
  }

  blur_brush::blur_brush(int passes) {
    this->passes = passes;    
  }


  // a grotty blur function, must do better...
  void blur_brush::render_span(image *target, int x, int y, int w) {
    uint32_t *dst = target->ptr(x, y);

    // prime the colour queues
    uint32_t l = target->pixel_clamped(x - 1, y); // first pixel (left)
    uint32_t m = target->pixel_clamped(x, y); // first pixel (middle)
    uint32_t r = target->pixel_clamped(x + 1, y); // first pixel (right)
    uint32_t rq = _r(l) << 16 | _r(m) << 8 | _r(r);
    uint32_t gq = _g(l) << 16 | _g(m) << 8 | _g(r);
    uint32_t bq = _b(l) << 16 | _b(m) << 8 | _b(r);
    uint32_t aq = _a(l) << 16 | _a(m) << 8 | _a(r);

    // horizontal pass    
    while(w--) {
      // average the three pixels in the queue...
      uint8_t r = (((rq >> 16) & 0xff) + ((rq >> 8) & 0xff) + ((rq >> 8) & 0xff) + ((rq >> 0) & 0xff)) >> 2;
      uint8_t g = (((gq >> 16) & 0xff) + ((gq >> 8) & 0xff) + ((gq >> 8) & 0xff) + ((gq >> 0) & 0xff)) >> 2;
      uint8_t b = (((bq >> 16) & 0xff) + ((bq >> 8) & 0xff) + ((bq >> 8) & 0xff) + ((bq >> 0) & 0xff)) >> 2;
      uint8_t a = (((aq >> 16) & 0xff) + ((aq >> 8) & 0xff) + ((aq >> 8) & 0xff) + ((aq >> 0) & 0xff)) >> 2;
     
      *dst = _make_col(r, g, b, a);
      dst++;

      // add the next pixel into the queue
      rq <<= 8;
      gq <<= 8;
      bq <<= 8;
      aq <<= 8;
      uint32_t n = target->pixel_clamped(x + 1, y); // new pixel (right)
      rq |= _r(n);
      gq |= _g(n);
      bq |= _b(n);
      aq |= _a(n);
      x++;
    }

  }


  void blur_brush::render_mask(image *target) {
    static uint32_t q[4];

    for(int i = 0; i < passes; i++) {
      // horizontal pass
      for(int y = 0; y < target->bounds.h; y++) {
        uint32_t *dst = target->ptr(0, y);

        // prime the sample queue
        q[0] = target->pixel_clamped(0, y);
        q[1] = target->pixel_clamped(0, y);
        q[2] = target->pixel_clamped(0, y);
        q[3] = target->pixel_clamped(1, y);

        for(int x = 0; x < target->bounds.w; x++) {
          // average the pixels in the queue...
          uint8_t r = (_r(q[0]) + _r(q[1]) + _r(q[2]) + _r(q[3])) >> 2;
          uint8_t g = (_g(q[0]) + _g(q[1]) + _g(q[2]) + _g(q[3])) >> 2;
          uint8_t b = (_b(q[0]) + _b(q[1]) + _b(q[2]) + _b(q[3])) >> 2;
          uint8_t a = (_a(q[0]) + _a(q[1]) + _a(q[2]) + _a(q[3])) >> 2;
        
          uint32_t col = _make_col(r, g, b, a);          
          _rgba_blend_to(dst, &col);
          dst++;

          // shuffle the queue
          q[0] = q[2];
          q[1] = q[3];
          q[2] = q[1];
          q[3] = target->pixel_clamped(x + 1, y);       
        }
      }

      // vertical pass
      for(int x = 0; x < target->bounds.w; x++) {
        uint32_t *dst = target->ptr(x, 0);

        // prime the sample queue
        q[0] = target->pixel_clamped(x, 0);
        q[1] = target->pixel_clamped(x, 0);
        q[2] = target->pixel_clamped(x, 0);
        q[3] = target->pixel_clamped(x, 1);

        for(int y = 0; y < target->bounds.h; y++) {
          // average the pixels in the queue...
          uint8_t r = (_r(q[0]) + _r(q[1]) + _r(q[2]) + _r(q[3])) >> 2;
          uint8_t g = (_g(q[0]) + _g(q[1]) + _g(q[2]) + _g(q[3])) >> 2;
          uint8_t b = (_b(q[0]) + _b(q[1]) + _b(q[2]) + _b(q[3])) >> 2;
          uint8_t a = (_a(q[0]) + _a(q[1]) + _a(q[2]) + _a(q[3])) >> 2;
        
          uint32_t col = _make_col(r, g, b, a);          
          _rgba_blend_to(dst, &col);
          dst += int(target->bounds.w);

          // shuffle and populate the queue
          q[0] = q[2];
          q[1] = q[3];
          q[2] = q[1];
          q[3] = target->pixel_clamped(x, y + 1);       
        }
      }

    }
  }

  brighten_brush::brighten_brush(int amount) : amount(amount) {}

  void brighten_brush::render_span(image *target, int x, int y, int w) {
    uint32_t *dst = target->ptr(x, y);

    while(w--) {
      uint8_t *pd = (uint8_t *)dst;

      int a = (pd[3] * amount) >> 8;

      int r = pd[0] + a;
      r = max(0, min(r, 255));
      pd[0] = r;

      int g = pd[1] + a;
      g = max(0, min(g, 255));
      pd[1] = g;

      int b = pd[2] + a;
      b = max(0, min(b, 255));
      pd[2] = b;

      dst++;
    }
  }

  xor_brush::xor_brush(int r, int g, int b) {
    this->color = _make_col(r, g, b);
  }

  void xor_brush::render_span(image *target, int x, int y, int w) {
    uint8_t *dst = (uint8_t*)target->ptr(x, y);
    uint8_t *src = (uint8_t*)&color;
    while(w--) {
      uint32_t xored = _make_col(dst[0] ^ src[0], dst[1] ^ src[1], dst[2] ^ src[2], src[3]);
      _rgba_blend_to((uint32_t *)dst, &xored);
      dst += 4;
    }
  }  


  // void xor::render_spans(image *target, shape *shape, render_span *spans, int count) {
  //   while(count--) {
  //     debug_printf("%d, %d (%d)\n", spans->x, spans->y, spans->w);

  //     uint32_t *dst = target->ptr(spans->x, spans->y);
  //     for(int i = 0; i < spans->w; i++) {
  //       uint8_t *pd = (uint8_t *)dst;
  //       pd[1] = ^pd[1];
  //       pd[2] = ^pd[2];
  //       pd[3] = ^pd[3];

  //       dst++;
  //     }
  //     spans++;
  //   }
  // }
}