#pragma once

#include "config.hpp"
#include <stdint.h>
#include <cassert>
#include <string.h>
#include <float.h>
#include <math.h>
#include <vector>
#include <algorithm>
#include <optional>

#include "mat3.hpp" // mat3_t (and, via util.hpp, PV_PI / fx16_t / clamp)

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

#include "picovector_working_buffer.h"


namespace picovector {

  #define debug_printf(fmt, ...) fprintf(stdout, fmt, ##__VA_ARGS__)

  class brush_t;
  class image_t;
  class shape_t;
  class glyph_t;
  class mat3_t;
  struct vec2_t;

#if PV_DUAL_CORE
  // Run `worker(ctx, y0, y1, step)` split across both cores by row parity: core1
  // takes the odd-offset rows (y0+1, y0+3, …), core0 the even (y0, y0+2, …), then
  // core0 joins. Synchronous — returns once both halves are done. Falls back to a
  // single in-line call when the range is too small to split. The worker must
  // touch only its own rows (disjoint dst rows ⇒ no locking needed on the shared,
  // coherent SRAM framebuffer). Reuses the rasteriser's core1 handshake.
  typedef void (*pv_row_worker_t)(void *ctx, int y0, int y1, int step);
  void pv_parallel_rows(pv_row_worker_t worker, void *ctx, int y0, int y1);
#endif

}