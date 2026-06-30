#pragma once

// Configuration: an embedder may provide a picovector.config.hpp on the include
// path (e.g. the picovector-micropython component) to override allocators and
// build knobs; config_default.hpp then fills in anything left unset.
#if __has_include("picovector.config.hpp")
#  include "picovector.config.hpp"
#endif
#include "config_default.hpp"
#include <stdint.h>
#include <cassert>
#include <string.h>
#include <float.h>
#include <math.h>
#include <vector>
#include <algorithm>
#include <optional>

#include "mat3.hpp" // brings in PV_PI (used across modules) and mat3_t

#ifndef PV_STD_ALLOCATOR
#define PV_STD_ALLOCATOR std::allocator
#endif

#ifndef PV_MALLOC
#define PV_MALLOC malloc
#endif

#ifndef PV_FREE
#define PV_FREE free
#endif

#ifndef PV_REALLOC
#define PV_REALLOC realloc
#endif

// TODO: bring back AA support
#include "picovector_working_buffer.h"


namespace picovector {

  #define debug_printf(fmt, ...) fprintf(stdout, fmt, ##__VA_ARGS__)

  class brush_t;
  class image_t;
  class shape_t;
  class glyph_t;
  class mat3_t;
  struct vec2_t;

  struct _rspan {
    int x; // span start x
    int y; // span y
    int w; // span width in pixels
    int o; // opacity of the span for blending (used for AA only)

    _rspan() : x(0), y(0), w(0), o(0) {}
    _rspan(int x, int y, int w, int o = 255) : x(x), y(y), w(w), o(o) {}
  };

  // Retained polygon renderer: begin a batch, add transformed paths (each
  // returns free edge slots, or -1 if it would overflow), then flush once.
  void render_begin();
  int  render_add_path(const vec2_t *pts, int count, mat3_t *transform);
  void render_flush(image_t *target, brush_t *brush);

  // Convenience wrapper for a whole shape.
  void render(shape_t *shape, image_t *target, mat3_t *transform, brush_t *brush);

  // Profiling hook — call once per frame (wired into image clear).
  void pv_profile_frame();

}