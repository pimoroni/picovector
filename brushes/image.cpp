#include "../brush.hpp"

namespace picovector {

  void image_brush_span_func(image_t *target, brush_t *brush, int x, int y, int w) {
    image_brush_t *p = (image_brush_t*)brush;
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

    for(int i = 0; i < w; i++) {
      pt.x += pd.x;
      pt.y += pd.y;
      int u = ((int(pt.x) >> 16) % tw + tw) % tw;
      int v = ((int(pt.y) >> 16) % th + th) % th;
      uint32_t c = p->src->get_unsafe(u, v);
      *dst = blend_over_premul(*dst, c);
      dst++;
    }
  }

  void image_brush_masked_span_func(image_t *target, brush_t *brush, int x, int y, int w, uint8_t *mask) {
    image_brush_t *p = (image_brush_t*)brush;
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

    for(int i = 0; i < w; i++) {
      pt.x += pd.x;
      pt.y += pd.y;
      int u = ((int(pt.x) >> 16) % tw + tw) % tw;
      int v = ((int(pt.y) >> 16) % th + th) % th;
      uint32_t c = p->src->get_unsafe(u, v);
      *dst = blend_over_premul(*dst, _premul_mul_alpha(c, *mask));
      dst++;
      mask++;
    }
  }

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

  span_func_t image_brush_t::span_func() {
    return image_brush_span_func;
  }

  masked_span_func_t image_brush_t::masked_span_func() {
    return image_brush_masked_span_func;
  }
}