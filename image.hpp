#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "config.hpp"
#include "types.hpp"
#include "blend.hpp"

using std::vector;

namespace picovector {

  class image_t;
  class brush_t;


  typedef void (*span_func_t)(image_t *target, brush_t *brush, int x, int y, int w);
  typedef void (*masked_span_func_t)(image_t *target, brush_t *brush, int x, int y, int w, uint8_t *mask);

  // Pre-clipped horizontal runs. A batch is homogeneous - either all solid (the
  // simple draw methods) or all masked (the AA rasteriser) - so the two never
  // mix in the buffer and there is no per-span mask test. Masked spans carry w
  // coverage bytes (a pointer into the rasteriser's tile buffer).
  struct pv_span        { int16_t x, y; uint16_t w; };                        // 6 bytes
  struct pv_masked_span { int16_t x, y; uint16_t w; const uint8_t *mask; };   // 12 bytes

  // A brush's batch blend reads the shared buffer via _spans()/_masked_spans()
  // and _num_spans(), so it takes only the target and brush.
  typedef void (*batch_span_func_t)(image_t *target, brush_t *brush);

  // One 8KB buffer, reinterpreted as whichever span type the current batch uses
  // (never both). Solid: ~1365 spans; masked: ~682. _span_n counts elements of
  // the active type. A 240-row solid fill can't overflow; masked batches are
  // per-tile (<=64 rows) but a busy tile can, so the AA emit flushes when full.
  static const int PV_SPAN_BYTES = 8192;
  static const int PV_MASKED_SPAN_CAP = PV_SPAN_BYTES / (int)sizeof(pv_masked_span);
  extern uint8_t _span_buf[PV_SPAN_BYTES];
  extern int _span_n;
  static inline void _reset_spans() { _span_n = 0; }
  static inline void _add_span(int x, int y, int w) {
    ((pv_span *)_span_buf)[_span_n++] = { (int16_t)x, (int16_t)y, (uint16_t)w };
  }
  static inline void _add_masked_span(int x, int y, int w, const uint8_t *mask) {
    ((pv_masked_span *)_span_buf)[_span_n++] = { (int16_t)x, (int16_t)y, (uint16_t)w, mask };
  }
  static inline const pv_span *_spans() { return (const pv_span *)_span_buf; }
  static inline const pv_masked_span *_masked_spans() { return (const pv_masked_span *)_span_buf; }
  static inline int _num_spans() { return _span_n; }

  typedef enum antialias_t {
    OFF   = 0,
    LOW   = 1,
    X2    = 1,
    HIGH  = 2,
    X4    = 2
  } antialias_t;

  typedef enum fill_rule_t {
    EVEN_ODD = 0, // default: inside toggles at every edge crossing
    NON_ZERO = 1  // inside where the accumulated winding number is non-zero
  } fill_rule_t;

  typedef enum pixel_format_t {
    RGBA8888 = 1,
    RGBA4444 = 2,
  } pixel_format_t;

  // texture sampling quality for the blit* functions
  typedef enum filter_t {
    NEAREST  = 0, // single nearest texel (fastest, default)
    BILINEAR = 1, // 2x2 linear blend
    BICUBIC  = 2  // 4x4 cubic (Catmull-Rom) blend
  } filter_t;

  typedef std::vector<uint32_t, PV_STD_ALLOCATOR<uint32_t>> palette_t;

  class mat3_t;
  class font_t;
  class pixel_font_t;
  class shape_t;
  class brush_t;

  class image_t {
    friend class brush_t;

    private:
      void              *_buffer = nullptr;
      bool               _managed_buffer = false;
      size_t             _row_stride;
      size_t             _bytes_per_pixel;

      rect_t             _bounds;
      rect_t             _clip;
      uint8_t            _alpha = 255;
      antialias_t        _antialias = OFF;
      fill_rule_t        _fill_rule = EVEN_ODD;
      pixel_format_t     _pixel_format = RGBA8888;
      bool               _has_palette = false;
      brush_t           *_brush = nullptr;
      font_t            *_font = nullptr;
      pixel_font_t      *_pixel_font = nullptr;
      palette_t          _palette;
      uint               _rows = 1, _cols = 1;   // spritesheet grid (1x1 = not a sheet)

    public:
      blend_func_t       _blend_func = blend_func_over;

      image_t();
      image_t(image_t *source, rect_t r);
      image_t(int w, int h, pixel_format_t pixel_format=RGBA8888, bool has_palette=false);
      image_t(int w, int h, int rows, int cols, pixel_format_t pixel_format=RGBA8888, bool has_palette=false);
      image_t(void *buffer, int w, int h, pixel_format_t pixel_format=RGBA8888, bool has_palette=false);
      image_t(void *buffer, int w, int h, int rows, int cols, pixel_format_t pixel_format=RGBA8888, bool has_palette=false);
      ~image_t();

      size_t buffer_size();
      size_t bytes_per_pixel();
      bool is_compatible(image_t *other);
      void window(image_t *source, rect_t viewport);
      image_t window(rect_t r);
      // return the sprite at grid cell (x, y) as a window, using the sheet's
      // _cols x _rows layout. x is the column, y is the row. A non-spritesheet
      // image is 1x1, so sprite(0, 0) is the whole image.
      image_t sprite(int x, int y);
      inline void* ptr(int x, int y) const {
        return (uint8_t *)(this->_buffer) + (x * this->_bytes_per_pixel) + (y * this->_row_stride);
      }
      uint32_t row_stride();

      rect_t bounds();
      rect_t clip();
      void clip(rect_t r);

      bool has_palette();
      // void delete_palette();
      void palette(uint8_t i, uint32_t c);
      uint32_t palette(uint8_t i);
      // raw palette storage for hot blit loops: skips the per-pixel out-of-line
      // palette(i) call and the by-value palette_t (std::vector) copy per span.
      inline const uint32_t* palette_data() const { return _palette.data(); }

      uint8_t alpha();
      void alpha(uint8_t alpha);

      antialias_t antialias();
      void antialias(antialias_t antialias);

      fill_rule_t fill_rule();
      void fill_rule(fill_rule_t fill_rule);

      // spritesheet grid used by sprite(); 1x1 = not a sheet
      uint rows();
      void rows(uint rows);
      uint cols();
      void cols(uint cols);

      pixel_format_t pixel_format();
      void pixel_format(pixel_format_t pixel_format);

      brush_t *brush();
      void brush(brush_t *brush);

      font_t *font();
      void font(font_t *font);

      pixel_font_t *pixel_font();
      void pixel_font(pixel_font_t *pixel_font);

      void span(int x, int y, int w);
      void masked_span(int x, int y, int w, uint8_t *mask);


      // raster primitives
      void clear();
      void rectangle(rect_t r);
      void triangle(vec2_t p1, vec2_t p2, vec2_t p3);
      void round_rectangle(const rect_t &r, int radius);
      void circle(const vec2_t &p, const int &r);
      void ellipse(const vec2_t &p, const int &rx, const int &ry);
      void line(vec2_t p1, vec2_t p2);

      // vector shapes
      void shape(shape_t *shape);

      // pixel accessors
      void put(const vec2_t &p1);
      void put(int x, int y);
      void put_unsafe(int x, int y);
      uint32_t get(const vec2_t &p1);
      uint32_t get(int x, int y);
      // hot texel fetch (brush/sample inner loops): inline so callers avoid the
      // per-pixel call; ptr() and _palette[] are already inline.
      inline uint32_t get_unsafe(int x, int y) const {
        if(this->_has_palette) return this->_palette[*((uint8_t *)ptr(x, y))];
        return *((uint32_t *)ptr(x, y));
      }

      // sample a (premultiplied) texel at fixed-point 16.16 source coordinates
      // using the requested filter; edge-clamped. Palette images always sample
      // NEAREST (indices can't be interpolated).
      uint32_t sample(fx16_t sx, fx16_t sy, filter_t filter);

      // filters
      void blur(float radius);
      void dither();
      void onebit();
      void monochrome();

      // blitting
      void blit(image_t *t, const vec2_t p);
      void blit(image_t *t, rect_t tr, filter_t filter = NEAREST);
      void blit(image_t *t, rect_t sr, rect_t tr, filter_t filter = NEAREST);
      void blit_hspan(image_t *target, vec2_t p, int c, vec2_t uv0, vec2_t uv1, filter_t filter = NEAREST);
      void blit_vspan(image_t *target, vec2_t p, int c, vec2_t uv0, vec2_t uv1, filter_t filter = NEAREST);

    private:
      // shared body for blit_hspan / blit_vspan: walks `c` texels sampling the
      // source along uv0->uv1. `vertical` selects the travel axis (y for vspan,
      // x for hspan), which drives both the clip and the destination step.
      void blit_span(image_t *target, vec2_t p, int c, vec2_t uv0, vec2_t uv1, filter_t filter, bool vertical);
      // Clip one horizontal run to _clip and, if any remains, add it to the
      // shared span buffer (no blend). Shared by span() and circle().
      void _span(int x, int y, int w);
  };

}
