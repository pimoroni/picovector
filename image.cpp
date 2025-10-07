#include "image.hpp"

#include "span.hpp"
#include "brush.hpp"

#include <string.h>
#include <math.h>
#include <algorithm>

using namespace std;



namespace picovector {

  color_brush _default_image_brush(255, 255, 255, 255);

  image::image(int w, int h) : managed_buffer(true), brush(&_default_image_brush) {
    bounds = rect(0, 0, w, h);
    p = (uint32_t *)PV_MALLOC(sizeof(uint32_t) * w * h);
    _rowstride = w * sizeof(uint32_t);
  }

  image::image(uint32_t *p, int w, int h) : p(p), managed_buffer(false), brush(&_default_image_brush) {
    bounds = rect(0, 0, w, h);    
    _rowstride = w * sizeof(uint32_t);
  }

  image::~image() {
    if(managed_buffer) {
      //PV_FREE(p);
    }
  }

  image image::window(rect r) {
    rect i = bounds.intersection(r);
    image window = image(ptr(r.x, r.y), i.w, i.h);

    // window.bounds = rect(0, 0, i.w, i.h);
    // window.p = ptr(r.x, r.y);
    window._rowstride = _rowstride;
    return window;
  }


  void image::clear() {
    rectangle(bounds);
  }

  void image::blit(image *t, const point p) {
    rect tr(p.x, p.y, bounds.w, bounds.h); // target rect
    tr = tr.intersection(t->bounds); // clip to target image bounds

    if(tr.empty()) {return;}

    int sxo = p.x < 0 ? -p.x : 0;
    int syo = p.y < 0 ? -p.y : 0;    

    for(int i = 0; i < tr.h; i++) {
      uint32_t *src = this->ptr(sxo, syo + i);
      uint32_t *dst = t->ptr(tr.x, tr.y + i);
      span_blit_argb8(src, dst, tr.w, this->alpha);
    }
  }

  void image::blit(image *target, rect tr) {
    // clip the target rect to the target bounds
    rect ctr = tr.intersection(target->bounds);
    if(ctr.empty()) {return;}

    // calculate the source step
    float srcstepx = this->bounds.w / tr.w;
    float srcstepy = this->bounds.h / tr.h;

    // calculate the source offset
    float srcx = ctr.w < 0 ? this->bounds.w : 0;
    float srcy = ctr.h < 0 ? this->bounds.h : 0;
    srcx += (ctr.x - tr.x) * srcstepx;
    srcy += (ctr.y - tr.y) * srcstepy;

    int sy = min(ctr.y, ctr.y + ctr.h);
    int ey = max(ctr.y, ctr.y + ctr.h);


    for(int y = sy; y != ey; y++) {
      uint32_t *dst = target->ptr(ctr.x, y);
      span_blit_scale(this->ptr(0, int(srcy)), dst, int(srcx * 65536.0f), int(srcstepx * 65536.0f), abs(ctr.w), this->alpha);
      srcy += srcstepy;
    }
  }

  uint32_t* image::ptr(int x, int y) {
    return this->p + x + (y * (this->_rowstride / sizeof(uint32_t)));
  }

  void image::draw(shape *shape) {
    mat3 m;
    render(shape, this, &m, brush);
  }

  void image::rectangle(const rect &r) {
    for(int y = 0; y < r.h; y++) {
      this->brush->render_span(this, r.x, y, r.w);
    }
  }


  void image::circle(const point &p, const int &r) {
    // int sy = max(p.y - r, 0);
    // int ey = min(p.y + r, bounds.h);
    // for(int y = sy; y < ey; y++) {
    //   int w = sqrt((r * r) - ((y - p.y) * (y - p.y)));
    //   int sx = p.x - w;
    //   int ex = p.x + w;
    //   if(ex < 0 || sx >= bounds.h) {continue;}
    //   sx = max(sx, 0);
    //   ex = min(ex, bounds.w);

    //   //printf("c: %d -> %d @ %d\n", sx, ex, y);
    //   span_argb8(ptr(sx, y), ex - sx, c);
    // }
  }


  uint32_t image::pixel(int x, int y) {
    return *ptr(x, y);    
  }

  uint32_t image::pixel_clamped(int x, int y) {
    x = max(int(bounds.x), min(x, int(bounds.x + bounds.w - 1)));
    y = max(int(bounds.y), min(y, int(bounds.y + bounds.h - 1)));
    return *ptr(x, y);    
  }

}