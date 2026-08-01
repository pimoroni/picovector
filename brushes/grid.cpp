#include "../brush.hpp"

namespace picovector {

  // See color.cpp for the brush-file layout. Gentle pixel grid: darken every
  // `spacing`-th row and column by `darkness` (0..255), leaving the cell
  // interiors untouched. Position-dependent (keyed on x, y in image space so the
  // grid stays screen-aligned).

  grid_brush_t::grid_brush_t(int spacing, int darkness)
    : spacing(spacing < 1 ? 1 : spacing), darkness(darkness) {}

  void grid_brush_t::blend_spans(image_t *target, int i0, int i1, int step) {
    const pv_span *spans = _spans();
    int f = 255 - darkness;
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y;
      bool yline = (y % spacing) == 0;
      uint8_t *p = (uint8_t*)target->ptr(x, y);
      for(int w = spans[i].w; w; w--) {
        if(yline || (x % spacing) == 0) {
          p[0] = (uint8_t)((p[0] * f) >> 8);
          p[1] = (uint8_t)((p[1] * f) >> 8);
          p[2] = (uint8_t)((p[2] * f) >> 8); // leave alpha
        }
        p += 4; x++;
      }
    }
  }

  void grid_brush_t::blend_masked_spans(image_t *target, int i0, int i1, int step) {
    const pv_masked_span *spans = _masked_spans();
    int f = 255 - darkness;
    for(int i = i0; i < i1; i += step) {
      int x = spans[i].x, y = spans[i].y;
      bool yline = (y % spacing) == 0;
      uint8_t *p = (uint8_t*)target->ptr(x, y);
      const uint8_t *mask = spans[i].mask;
      // on grid lines, ease each channel toward the darkened value by coverage
      for(int w = spans[i].w; w; w--) {
        int m = *mask++;
        if(m && (yline || (x % spacing) == 0)) {
          int n0 = (p[0] * f) >> 8, n1 = (p[1] * f) >> 8, n2 = (p[2] * f) >> 8;
          p[0] = (uint8_t)(p[0] + (((n0 - p[0]) * m) >> 8));
          p[1] = (uint8_t)(p[1] + (((n1 - p[1]) * m) >> 8));
          p[2] = (uint8_t)(p[2] + (((n2 - p[2]) * m) >> 8));
        }
        p += 4; x++;
      }
    }
  }

}
