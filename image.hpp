#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <new>   // placement new (make_scratch_image)

#include "config.hpp"
#include "types.hpp"
#include "blend.hpp"
#include "pixel_store.hpp"
#include "picovector_working_buffer.h"   // make_scratch_image()

using std::vector;

namespace picovector {

  class image_t;
  class brush_t;

  // Persistent text caret, so image.text() can omit its position (continuing
  // where the last text() left off) and honour '\n'. `origin_x` is the column a
  // newline returns to; `line_height` is the per-newline y advance (both set up
  // per text() call from the position and font). `valid` is false until the
  // first positioned draw / explicit set, when it defaults the origin to (0, 0).
  struct text_cursor_t {
    float x = 0.0f, y = 0.0f;
    float origin_x = 0.0f;
    float line_height = 0.0f;
    bool  valid = false;
  };



  // Pre-clipped horizontal runs. A batch is homogeneous - either all solid (the
  // simple draw methods) or all masked (the AA rasteriser) - so the two never
  // mix in the buffer and there is no per-span mask test. Masked spans carry w
  // coverage bytes (a pointer into the rasteriser's tile buffer).
  struct pv_span        { int16_t x, y; uint16_t w; };                        // 6 bytes
  struct pv_masked_span { int16_t x, y; uint16_t w; const uint8_t *mask; };   // 12 bytes

  // A brush's blend_spans()/blend_masked_spans() composite the span index range
  // [i0, i1) stepping by `step`, reading the shared buffer via _spans()/
  // _masked_spans(). Single-core callers pass (0, n, 1); the dual-core dispatcher
  // splits by span-index parity (core0: 0,2,4…; core1: 1,3,5…) so the two cores
  // write disjoint framebuffer rows without locking.

  // One 8KB buffer, reinterpreted as whichever span type the current batch uses
  // (never both). Solid: ~1365 spans; masked: ~682. _span_n counts elements of
  // the active type.
  //
  // Nothing bounds what an emitter produces: a tile row holds up to
  // MAX_NODES_PER_SCANLINE/2 spans, line() emits one per pixel, draw_glyph()
  // multiplies runs by scale, and a target can be taller than the buffer. So
  // emitters go through _emit_span, which blends and resets a full batch before
  // storing. The one bounds test both splits the batch and makes an overrun
  // unreachable, so there is no cheaper unchecked path to reach for.
  static const int PV_SPAN_BYTES = 8192;
  static const int PV_MASKED_SPAN_CAP = PV_SPAN_BYTES / (int)sizeof(pv_masked_span);
  static const int PV_SOLID_SPAN_CAP = PV_SPAN_BYTES / (int)sizeof(pv_span);
  extern uint8_t _span_buf[PV_SPAN_BYTES];
  extern int _span_n;
  void _blend_spans(image_t *target, brush_t *brush);
  void _blend_masked_spans(image_t *target, brush_t *brush);

  // Blending a full batch part-way through an emit is the rare path, so it is
  // kept out of line and marked cold. Inlining it into the emitters cost ~7% on
  // circle(), which emits many short spans: the call clobbers the loop's
  // registers whether or not it is reached. Outlined, the check is free.
  __attribute__((noinline, cold)) void _flush_solid_spans(image_t *target, brush_t *brush);
  __attribute__((noinline, cold)) void _flush_masked_spans(image_t *target, brush_t *brush);

  static inline void _reset_spans() { _span_n = 0; }
  static inline void _add_span(int x, int y, int w) {
    ((pv_span *)_span_buf)[_span_n++] = { (int16_t)x, (int16_t)y, (uint16_t)w };
  }
  static inline void _add_masked_span(int x, int y, int w, const uint8_t *mask) {
    ((pv_masked_span *)_span_buf)[_span_n++] = { (int16_t)x, (int16_t)y, (uint16_t)w, mask };
  }

  // Spans are independent runs of pixels, so where a batch splits does not
  // change the result.
  static inline void _emit_span(image_t *target, brush_t *brush, int x, int y, int w) {
    if(__builtin_expect(_span_n >= PV_SOLID_SPAN_CAP, 0)) _flush_solid_spans(target, brush);
    _add_span(x, y, w);
  }
  static inline void _emit_masked_span(image_t *target, brush_t *brush, int x, int y, int w, const uint8_t *mask) {
    if(__builtin_expect(_span_n >= PV_MASKED_SPAN_CAP, 0)) _flush_masked_spans(target, brush);
    _add_masked_span(x, y, w, mask);
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

  // Error correction level for image_t::qr(), ascending redundancy. More
  // correction survives more damage but needs a larger code for the same text.
  // Values match enum qrcodegen_Ecc.
  typedef enum qr_ecc_t {
    QR_LOW      = 0,
    QR_MEDIUM   = 1,
    QR_QUARTILE = 2,
    QR_HIGH     = 3
  } qr_ecc_t;

  // texture sampling quality for the blit* functions
  typedef enum filter_t {
    NEAREST  = 0, // single nearest texel (fastest, default)
    BILINEAR = 1, // 2x2 linear blend
    BICUBIC  = 2  // 4x4 cubic (Catmull-Rom) blend
  } filter_t;

  // Text alignment within image.text()'s bounds. One shared axis so a single
  // CENTER reads for both: LEFT/CENTER/RIGHT horizontally, TOP/MIDDLE/BOTTOM
  // vertically (the aliases share a value so align=(CENTER, CENTER) works).
  typedef enum text_align_t {
    LEFT   = 0, TOP    = 0,
    CENTER = 1, MIDDLE = 1,
    RIGHT  = 2, BOTTOM = 2
  } text_align_t;

  // How image.text() handles text that overflows its bounds vertically.
  typedef enum text_overflow_t {
    CLIP     = 0, // clip to the bounds
    ELLIPSES = 1  // truncate the last visible line with a trailing "..."
  } text_overflow_t;


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
      // Sized to what the source actually needs (a 4-bit PNG has 16 entries, not
      // 256) and shared with sub-views rather than copied: a spritesheet exists
      // to be sliced, and a view already depends on its parent outliving it for
      // the pixels, so the palette rides along on the same contract.
      uint32_t          *_palette = nullptr;
      uint16_t           _palette_size = 0;
      bool               _owns_palette = false;
      brush_t           *_brush = nullptr;
      font_t            *_font = nullptr;
      pixel_font_t      *_pixel_font = nullptr;
      text_cursor_t      _text_cursor;
      uint32_t               _rows = 1, _cols = 1;   // grid a decoder recorded

    public:
      blend_func_t       _blend_func = blend_func_over;

      image_t();
      image_t(image_t *source, rect_t r);
      image_t(int w, int h, pixel_format_t pixel_format=RGBA8888, bool has_palette=false, int palette_entries=256);
      image_t(int w, int h, int rows, int cols, pixel_format_t pixel_format=RGBA8888, bool has_palette=false, int palette_entries=256);
      image_t(void *buffer, int w, int h, pixel_format_t pixel_format=RGBA8888, bool has_palette=false, int palette_entries=256);
      image_t(void *buffer, int w, int h, int rows, int cols, pixel_format_t pixel_format=RGBA8888, bool has_palette=false, int palette_entries=256);
      ~image_t();

      // A QR code encoding `text`: one pixel per module, plus a `border` module
      // quiet zone on each side, as a two-entry palette image (0 light, 1 dark).
      // Scale it up when blitting rather than here, so the code costs its module
      // count and no more. Returns nullptr if `text` does not fit at this error
      // correction level, or on allocation failure. Caller owns the result and
      // frees it as it would any other allocated image.
      static image_t *qr(const char *text, qr_ecc_t ecc=QR_MEDIUM, int border=4);

      // The pixels this image owns, tightly packed: what an allocation needs.
      size_t buffer_size();
      // Every byte reachable from ptr(0, 0), which for a view is the rows of the
      // *parent* it spans - its own rows are a stride apart, so the last of them
      // sits well past buffer_size() bytes in. Equal to buffer_size() whenever
      // the image owns its buffer, since then a row is exactly a row.
      size_t buffer_extent();
      size_t bytes_per_pixel();
      void window(image_t *source, rect_t viewport);
      image_t window(rect_t r);
      inline void* ptr(int x, int y) const {
        return (uint8_t *)(this->_buffer) + (x * this->_bytes_per_pixel) + (y * this->_row_stride);
      }
      uint32_t row_stride();

      rect_t bounds();
      rect_t clip();
      void clip(rect_t r);

      // The integer pixel rect a whole-image filter may write: the clip,
      // intersected with the bounds. Filters still *sample* the whole image -
      // their geometry is image-relative, and a blur or a zoom legitimately
      // reads neighbours from outside - so this bounds the writes only. Returns
      // false when nothing is visible.
      inline bool filter_rect(int &x0, int &y0, int &x1, int &y1) const {
        rect_t cr = _clip.intersection(_bounds);
        if(cr.empty()) return false;
        x0 = (int)cr.x;            y0 = (int)cr.y;
        x1 = (int)(cr.x + cr.w);   y1 = (int)(cr.y + cr.h);
        return x1 > x0 && y1 > y0;
      }

      bool has_palette();
      // void delete_palette();
      void palette(uint8_t i, uint32_t c);
      uint32_t palette(uint8_t i);
      // raw palette storage for hot blit loops: skips the per-pixel out-of-line
      // palette(i) call.
      inline const uint32_t* palette_data() const { return _palette; }
      // Mutable overload, for a caller handing the table out as writable bytes.
      // Sub-views share this pointer rather than copying it, so a write through
      // it reaches every sprite cut from the same sheet - which is the point.
      inline uint32_t* palette_data() { return _palette; }
      inline int palette_size() const { return _palette_size; }

      uint8_t alpha();
      void alpha(uint8_t alpha);

      antialias_t antialias();
      void antialias(antialias_t antialias);

      fill_rule_t fill_rule();
      void fill_rule(fill_rule_t fill_rule);

      // The grid a decoder recorded, for spritesheet() to read back; 1x1 = none.
      // Carving cells out of it belongs to spritesheet_t, not here.
      uint32_t rows();
      void rows(uint32_t rows);
      uint32_t cols();
      void cols(uint32_t cols);

      pixel_format_t pixel_format();
      void pixel_format(pixel_format_t pixel_format);

      brush_t *brush();
      void brush(brush_t *brush);

      font_t *font();
      void font(font_t *font);

      pixel_font_t *pixel_font();
      void pixel_font(pixel_font_t *pixel_font);

      // Text caret. The vec2 getter/setter back the `image.cursor` property;
      // setting it establishes the newline origin (origin_x = x) and marks it
      // valid. text_cursor_state() is the mutable state the text draw path
      // reads/advances.
      vec2_t text_cursor();
      void text_cursor(vec2_t p);
      text_cursor_t *text_cursor_state() { return &_text_cursor; }

      void span(int x, int y, int w);
      // horizontal / vertical single-run fills (span buffer + batch blend)
      void hspan(int x, int y, int w);
      void vspan(int x, int y, int h);
      void masked_span(int x, int y, int w, uint8_t *mask);


      // raster primitives
      void clear();
      void rectangle(rect_t r);
      void triangle(vec2_t p1, vec2_t p2, vec2_t p3);
      void circle(const vec2_t &p, const int &r);
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
        return pv_load((const pv_store_t *)ptr(x, y));
      }

      // sample a (premultiplied) texel at fixed-point 16.16 source coordinates
      // using the requested filter; edge-clamped. Palette images always sample
      // NEAREST (indices can't be interpolated).
      uint32_t sample(fx16_t sx, fx16_t sy, filter_t filter);

      // filters
      void blur(float radius, float strength = 1.0f);
      void bloom(int threshold, int intensity, float radius, float strength = 1.0f);
      void edgeglow(int strength);
      void wave(int horizontal, int vertical, float strength = 1.0f, bool bilinear = false);
      void zoom(int strength);
      void dither();
      void onebit();
      void monochrome();
      void invert();
      void threshold(int level, color_t lo, color_t hi);
      void saturation(int amount);
      void contrast(int amount);
      void duotone(color_t shadow, color_t highlight);
      void crt(int spacing, int darkness, float strength = 1.0f);
      void grid(int spacing, int darkness, float strength = 1.0f);
      void vignette(int strength);
      void gameboy();
      void noise(int amount, int interval, float strength = 1.0f);
      void glitch(int amount, float strength = 1.0f);
      void oilpaint(int radius, int strength);
      void cga();
      void palette_dither(const uint32_t *colors, int n, int strength);
      void phosphor(color_t tint);
      void synthwave();
      void c64();
      void nightvision();
      void chromatic(int offset, float strength = 1.0f);

      // blitting
      void blit(image_t *t, const vec2_t p);
      void blit(image_t *t, rect_t tr, filter_t filter = NEAREST);
      void blit(image_t *t, rect_t sr, rect_t tr, filter_t filter = NEAREST);
      void blit_hspan(image_t *target, vec2_t p, float len, vec2_t uv0, vec2_t uv1, filter_t filter = NEAREST);
      void blit_vspan(image_t *target, vec2_t p, float len, vec2_t uv0, vec2_t uv1, filter_t filter = NEAREST);

    private:
      // shared body for blit_hspan / blit_vspan: walks `c` texels sampling the
      // source along uv0->uv1. `vertical` selects the travel axis (y for vspan,
      // x for hspan), which drives both the clip and the destination step.
      void blit_span(image_t *target, vec2_t p, float len, vec2_t uv0, vec2_t uv1, filter_t filter, bool vertical);
      // Clip one horizontal run to _clip and, if any remains, add it to the
      // shared span buffer (no blend). Shared by span() and circle().
      void _span(int x, int y, int w);
      // allocate this image's own palette (owner only)
      void alloc_palette(int entries);
  };

  // Allocate a w x h scratch image to work in: it wraps the shared working
  // buffer when the pixels fit (no pixel allocation), else owns a managed buffer.
  // Returns nullptr on allocation failure. Pass the result to free_scratch_image()
  // when done (the working buffer is shared and left alone; a managed buffer is
  // freed).
  inline image_t *make_scratch_image(int w, int h) {
    if(w < 1 || h < 1) return nullptr;
    void *mem = PV_MALLOC(sizeof(image_t));
    if(!mem) return nullptr;
    if((size_t)w * h * sizeof(pv_store_t) <= working_buffer_size)
      return new (mem) image_t((void*)PicoVector_working_buffer, w, h);
    return new (mem) image_t(w, h);
  }

  inline void free_scratch_image(image_t *img) {
    if(!img) return;
    img->~image_t();
#if MICROPY_MALLOC_USES_ALLOCATED_SIZE
    PV_FREE(img, sizeof(image_t));
#else
    PV_FREE(img);
#endif
  }

}
