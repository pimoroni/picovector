#include "../brush.hpp"

namespace picovector {

  image_brush_t::image_brush_t(image_t *src) : src(src) {
  }

  image_brush_t::image_brush_t(image_t *src, mat3_t *transform) : src(src) {
    if(transform) {
      base_inverse = *transform;
      base_inverse.inverse();
    }
    inverse_transform = base_inverse; // no shape transform applied yet
  }

  // Fold the shape's transform into the texture mapping so the image tracks the
  // shape: device->image = (brush's own inverse) * inverse(shape transform).
  void image_brush_t::set_render_transform(mat3_t *transform) {
    if(!transform) { inverse_transform = base_inverse; return; }
    mat3_t inv = *transform;
    inv.inverse();
    inverse_transform = base_inverse;
    inverse_transform.multiply(inv);
  }

  void image_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    image_brush_t *p = this;
    const pv_span *spans = _spans();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      uint32_t *dst = (uint32_t*)target->ptr(x, y);
      rect_t b = p->src->bounds();

      fx16_vec2_t p1(x, y);
      fx16_vec2_t p2((x + w), y);

      p1 = p1.transform(&p->inverse_transform);
      p2 = p2.transform(&p->inverse_transform);

      fx16_vec2_t pd((p2.x - p1.x) / w, (p2.y - p1.y) / w);
      fx16_vec2_t pt = p1;

      int tw = int(b.w);
      int th = int(b.h);

      for(int j = 0; j < w; j++) {
        pt.x += pd.x;
        pt.y += pd.y;
        int u = ((int(pt.x) >> 16) % tw + tw) % tw;
        int v = ((int(pt.y) >> 16) % th + th) % th;
        uint32_t c = p->src->get_unsafe(u, v);
        *dst = blend_over_premul(*dst, c);
        dst++;
      }
    }
  }

  void image_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    image_brush_t *p = this;
    const pv_masked_span *spans = _masked_spans();
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y, w = spans[i].w;
      uint8_t *mask = (uint8_t*)spans[i].mask;
      uint32_t *dst = (uint32_t*)target->ptr(x, y);
      rect_t b = p->src->bounds();

      fx16_vec2_t p1(x, y);
      fx16_vec2_t p2((x + w), y);

      p1 = p1.transform(&p->inverse_transform);
      p2 = p2.transform(&p->inverse_transform);

      fx16_vec2_t pd((p2.x - p1.x) / w, (p2.y - p1.y) / w);
      fx16_vec2_t pt = p1;

      int tw = int(b.w);
      int th = int(b.h);

      for(int j = 0; j < w; j++) {
        // the source position steps whether or not the pixel is covered
        pt.x += pd.x;
        pt.y += pd.y;
        uint32_t m = *mask++;
        if(m) {
          int u = ((int(pt.x) >> 16) % tw + tw) % tw;
          int v = ((int(pt.y) >> 16) % th + th) % th;
          blend_masked_over_premul(dst, p->src->get_unsafe(u, v), m);
        }
        dst++;
      }
    }
  }

}
