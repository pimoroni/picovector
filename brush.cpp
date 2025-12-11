#include "brush.hpp"
#include "image.hpp"
#include "blend.hpp"

namespace picovector {

  void color_brush::render_span(image_t *target, int x, int y, int w) {
    _span_blend_rgba_rgba((uint8_t*)target->ptr(x, y), (uint8_t*)&color, w);
  }

  void color_brush::render_span_buffer(image_t *target, int x, int y, int w, uint8_t *sb) {
    _span_blend_rgba_rgba_masked((uint8_t*)target->ptr(x, y), (uint8_t*)&color, sb, w);
  }




  void brighten_brush::render_span(image_t *target, int x, int y, int w) {
    uint32_t *dst = (uint32_t*)target->ptr(x, y);

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

  void xor_brush::render_span(image_t *target, int x, int y, int w) {
    uint8_t *dst = (uint8_t*)target->ptr(x, y);
    uint8_t *src = (uint8_t*)&color;
    while(w--) {
      uint32_t xored = _make_col(dst[0] ^ src[0], dst[1] ^ src[1], dst[2] ^ src[2], src[3]);
      _blend_rgba_rgba(dst, (uint8_t*)&xored);
      dst += 4;
    }
  }


  void xor_brush::render_span_buffer(image_t *target, int x, int y, int w, uint8_t *sb) {
    uint8_t *dst = (uint8_t*)target->ptr(x, y);
    uint8_t *src = (uint8_t*)&color;
    while(w--) {
      uint32_t xored = _make_col(dst[0] ^ src[0], dst[1] ^ src[1], dst[2] ^ src[2], *sb);
      _blend_rgba_rgba(dst, (uint8_t*)&xored);
      dst += 4;
      sb++;
    }
  }



  void pattern_brush::render_span(image_t *target, int x, int y, int w) {
    uint8_t *dst = (uint8_t*)target->ptr(x, y);

    uint8_t *src1 = (uint8_t*)&c1;
    uint8_t *src2 = (uint8_t*)&c2;

    while(w--) {
      uint8_t u = 7 - (x & 0b111);
      uint8_t v = y & 0b111;
      uint8_t b = p[v];
      uint8_t *src = b & (1 << u) ? src1 : src2;
      _blend_rgba_rgba(dst, src);
      dst += 4;
      x++;
    }
  }


  void pattern_brush::render_span_buffer(image_t *target, int x, int y, int w, uint8_t *sb) {
    uint8_t *dst = (uint8_t*)target->ptr(x, y);

    uint8_t *src1 = (uint8_t*)&c1;
    uint8_t *src2 = (uint8_t*)&c2;

    while(w--) {
      uint8_t u = 7 - (x & 0b111);
      uint8_t v = y & 0b111;
      uint8_t b = p[v];
      uint8_t *src = b & (1 << u) ? src1 : src2;
      _blend_rgba_rgba(dst, src, *sb);
      dst += 4;
      x++;
      sb++;
    }
  }

  void image_brush::render_span(image_t *target, int x, int y, int w) {
    uint8_t *dst = (uint8_t*)target->ptr(x, y);

    rect_t b = src->bounds();

    point_t p1(x, y);
    point_t p2((x + w), y);

    p1 = p1.transform(&this->it);
    p2 = p2.transform(&this->it);

    point_t pd((p2.x - p1.x) / w, (p2.y - p1.y) / w);
    point_t p = p1;

    int tw = int(b.w);
    int th = int(b.h);

    for(int i = 0; i < w; i++) {
      p.x += pd.x;
      p.y += pd.y;
      int u = (int(p.x) % tw + tw) % tw;
      int v = (int(p.y) % th + th) % th;
      uint32_t c = src->get_unsafe(u, v);
      _blend_rgba_rgba(dst, (uint8_t*)&c);
      dst += 4;
      dst += 4;
    }
  }


  void image_brush::render_span_buffer(image_t *target, int x, int y, int w, uint8_t *sb) {
    uint8_t *dst = (uint8_t*)target->ptr(x, y);
    rect_t b = src->bounds();

    point_t p1(x, y);
    point_t p2((x + w), y);

    p1 = p1.transform(&this->it);
    p2 = p2.transform(&this->it);

    point_t pd((p2.x - p1.x) / w, (p2.y - p1.y) / w);
    point_t p = p1;

    int tw = int(b.w);
    int th = int(b.h);

    while(w--) {
      p.x += pd.x;
      p.y += pd.y;
      int u = (int(p.x) % tw + tw) % tw;
      int v = (int(p.y) % th + th) % th;
      uint32_t c = src->get_unsafe(u, v);
      _blend_rgba_rgba(dst, (uint8_t*)&c, *sb);
      dst += 4;
      sb++;
    }
  }

}