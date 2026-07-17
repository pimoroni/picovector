#include <algorithm>
#include <cfloat>

// PV_PROFILE (rasteriser phase profiling) and PV_DUAL_CORE (rasterise on core1)
// come from the picovector config — both default OFF (see config_default.hpp).
// The Badgeware/MicroPython build sets PV_DUAL_CORE=1 via picovector-micropython.cmake.
// picovector.hpp is included first so those macros are defined before use below.
#include "picovector.hpp"

#if PV_PROFILE
#include <cstdio>
#endif

#include "brush.hpp"
#include "image.hpp"
#include "shape.hpp"
#include "font.hpp"
#include "types.hpp"
#include "mat3.hpp"
#include "blend.hpp"

using std::sort, std::min, std::max;

// The rasterisation memory pool (PicoVector_working_buffer) and its size now
// live in picovector_working_buffer.cpp so the size is configurable
// (PV_WORKING_BUFFER_SIZE) and the buffer can be shared with embedders that use
// it as scratch (e.g. PNG/JPEG decode in the MicroPython bindings). The extern
// declarations come in via picovector.hpp.

// ---------------------------------------------------------------------------
// core1 SDK glue. There is a single core1 worker for the whole component — the
// rasteriser's dispatcher (further down, inside namespace picovector) owns it
// and launches it lazily via pv_core1_launch(). Work is handed off via shared
// memory, NOT the inter-core FIFO: MicroPython owns the FIFO (its lockout-victim
// IRQ on core0 would consume our messages), so the FIFO is only used — with that
// IRQ briefly gated — by multicore_launch_core1 to start the core. The blur
// filter reuses this same worker through pv_core1_run()/pv_core1_join(), defined
// alongside the dispatcher so both cannot fight over launching core1. Enabled
// only when the pico SDK's multicore header is present.
// ---------------------------------------------------------------------------

#if PV_DUAL_CORE
extern "C" {
  // forward-declared to avoid a hard SDK include dependency (linked into the firmware)
  void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack_bottom, size_t stack_size_bytes);
  void irq_set_enabled(unsigned int num, bool enabled);
}
#define PV_SIO_FIFO_IRQ 25 // SIO_IRQ_FIFO on RP2350 (core0's FIFO IRQ)
#endif

// A tile spans the whole lores screen, so any shape rasterises in a single tile /
// single pass. For the signed-area path this also means no interior tile
// boundaries, so edge coverage is exact everywhere (no cross-tile clamping).
#define TILE_WIDTH 160
#define TILE_HEIGHT 120
#define MAX_NODES_PER_SCANLINE 32 // edge crossings per scanline; guarded in
                                  // add_line_segment_to_nodes. Extras beyond 32 are
                                  // dropped (convex = 2; a busy scanline rarely nears it).

// The aa==0 hard-edge path stores one crossing list per output scanline, so it
// needs one node row per tile row (no supersampling - that's the signed-area
// path's job).
#define NODE_BUFFER_ROWS TILE_HEIGHT // one node row per tile scanline

#define TILE_BUFFER_SIZE (TILE_WIDTH * (TILE_HEIGHT + 1) * sizeof(uint8_t)) // ~19kB coverage buffer
#define NODE_BUFFER_ROW_SIZE (MAX_NODES_PER_SCANLINE * sizeof(int16_t))
#define NODE_BUFFER_SIZE (NODE_BUFFER_ROWS * NODE_BUFFER_ROW_SIZE) // node buffer
#define NODE_COUNT_BUFFER_SIZE (NODE_BUFFER_ROWS * sizeof(uint8_t)) // node count buffer

// edge accumulator for the retained renderer (begin / add_path / flush). Each
// path's points are mat3-transformed once into device-space edges here, then the
// flush rasterises the whole batch in one pass.
struct edge_t { float x0, y0, x1, y1; };
#define MAX_EDGES 1024 // covers the worst single shape (world map max = 556 pts)
#define EDGE_BUFFER_SIZE (MAX_EDGES * (int)sizeof(edge_t))          // 16kB
#define SA_ACC_SIZE      (TILE_WIDTH * TILE_HEIGHT * (int)sizeof(int16_t)) // Q11 accumulator

// Working-buffer layout. tile_buffer (coverage) and edge_buffer are used by both
// rasteriser paths. The node buffers (aa==0 path) and the signed-area accumulator
// sa_acc are mutually exclusive - a render_flush is either aa==0 (nodes) or aa>0
// (signed area) - so they overlay ONE shared region rather than each costing SRAM.
#define AA_REGION_OFF    (TILE_BUFFER_SIZE + EDGE_BUFFER_SIZE)
#define NODE_REGION_SIZE (NODE_BUFFER_SIZE + NODE_COUNT_BUFFER_SIZE)
#define AA_REGION_SIZE   (NODE_REGION_SIZE > SA_ACC_SIZE ? NODE_REGION_SIZE : SA_ACC_SIZE)
static_assert(AA_REGION_OFF + AA_REGION_SIZE <= PV_WORKING_BUFFER_SIZE,
              "PicoVector working buffer too small for a full-screen tile");

uint8_t *tile_buffer       = (uint8_t *)&PicoVector_working_buffer[0];
edge_t  *edge_buffer       = (edge_t *)&PicoVector_working_buffer[TILE_BUFFER_SIZE];
int16_t *node_buffer       = (int16_t *)&PicoVector_working_buffer[AA_REGION_OFF];
uint8_t *node_count_buffer = (uint8_t *)&PicoVector_working_buffer[AA_REGION_OFF + NODE_BUFFER_SIZE];
int16_t *sa_acc            = (int16_t *)&PicoVector_working_buffer[AA_REGION_OFF]; // shares the node region
static int edge_count = 0;
static float acc_minx, acc_miny, acc_maxx, acc_maxy; // running device-space bounds

// --- rasteriser profiling (toggle with PV_PROFILE above) ----------
// Counts/times accumulate per frame; pv_profile_frame() (called once a frame
// from image clear) prints the last sampled frame about once a second.
#if PV_PROFILE
extern "C" uint64_t time_us_64(void);
extern "C" void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len); // MicroPython REPL output
static uint32_t pv_paths = 0, pv_edges = 0, pv_pixels = 0;
static uint64_t pv_t_transform = 0, pv_t_build = 0, pv_t_raster = 0;
static uint64_t pv_last_print = 0;
static uint64_t pv_last_clear = 0; // timestamp of the previous clear() — for whole-frame FPS
#define PV_T0(v)       uint64_t v = time_us_64()
#define PV_ADD(acc, v) (acc) += time_us_64() - (v)
#define PV_CNT(acc, n) (acc) += (n)
#else
#define PV_T0(v)
#define PV_ADD(acc, v)
#define PV_CNT(acc, n)
#endif
// ------------------------------------------------------------------

static inline void insertion_sort_i16(int16_t* a, int n) {
  for (int i = 1; i < n; ++i) {
    int16_t key = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > key) {
      a[j + 1] = a[j];
      --j;
    }
    a[j + 1] = key;
  }
}

namespace picovector {

  int sign(int v) {return (v > 0) - (v < 0);}

  // Record where this edge crosses each scanline of the tile (used by the aa==0
  // hard-edge path). The crossing x and winding direction are packed into one
  // node per scanline the edge spans.
  void add_line_segment_to_nodes(vec2_t start, vec2_t end, rect_t *tb) {
    // winding direction: downward edges wind +1, upward (swapped) edges -1
    int dir_bit = 0;
    if(end.y < start.y) {
      vec2_t tmp = start; start = end; end = tmp;
      dir_bit = 1;
    }

    if (end.y < 0.0f || start.y > tb->h || end.y == start.y) return;

    float x = start.x;
    float dx = (end.x - start.x) / (end.y - start.y);

    if(start.y < 0.0f) {
      x += -start.y * dx; // start.y < 0, so |start.y| == -start.y (avoids double-precision fabs)
      start.y = 0.0f;
    }

    if(end.y > tb->h) {
      end.y = tb->h;
    }

    int minx = 0;
    int maxx = ceilf(tb->w);

    int sy = int(start.y);
    int ey = int(end.y);

    for(int iy = sy; iy < ey; iy++) {
      int ix = max(min(int(x), maxx), minx);
      int row = iy;

      // pack: x in the high bits, winding direction in bit 0 (tile-local x fits).
      // Guard the per-scanline cap: a scanline with more than MAX_NODES_PER_SCANLINE
      // crossings drops the extras rather than overflowing into the next row's
      // slots. This only bites pathological self-overlapping geometry (a dropped
      // crossing is a minor local fill artifact, not memory corruption).
      uint8_t cnt = node_count_buffer[row];
      if(cnt < MAX_NODES_PER_SCANLINE) {
        node_buffer[(row * MAX_NODES_PER_SCANLINE) + cnt] = (ix << 1) | dir_bit;
        node_count_buffer[row] = cnt + 1;
      }

      x += dx;
    }
  }

  // --- retained renderer: begin / add_path(...) x N / flush -----------------

  // Start a new batch.
  void render_begin() {
    edge_count = 0;
    acc_minx = acc_miny = FLT_MAX;
    acc_maxx = acc_maxy = -FLT_MAX;
  }

  // Transform pts[i0, i1) by the affine coeffs, writing each edge (transformed
  // previous point -> transformed current point) into edge_buffer[base + i], and
  // returning this range's device-space bounds. The previous point wraps to
  // count-1 at i==0, so any contiguous sub-range emits exactly the edges it would
  // as part of the whole closed loop — which lets the two cores split a path.
  //   x' = a*x + c*y + e,  y' = b*x + d*y + f
  static void transform_points_range(const vec2_t *pts, int count, int i0, int i1, int base,
                                     float a, float b, float c, float d, float e, float f,
                                     float &minx, float &miny, float &maxx, float &maxy) {
    int pi = (i0 == 0) ? (count - 1) : (i0 - 1);
    float px = a * pts[pi].x + c * pts[pi].y + e;
    float py = b * pts[pi].x + d * pts[pi].y + f;

    float lminx = FLT_MAX, lminy = FLT_MAX, lmaxx = -FLT_MAX, lmaxy = -FLT_MAX;
    for(int i = i0; i < i1; i++) {
      float cx = a * pts[i].x + c * pts[i].y + e;
      float cy = b * pts[i].x + d * pts[i].y + f;

      edge_buffer[base + i] = { px, py, cx, cy };

      if(cx < lminx) lminx = cx;
      if(cx > lmaxx) lmaxx = cx;
      if(cy < lminy) lminy = cy;
      if(cy > lmaxy) lmaxy = cy;

      px = cx; py = cy;
    }
    minx = lminx; miny = lminy; maxx = lmaxx; maxy = lmaxy;
  }

  // Transform a path's points once and append its edges to the batch, growing
  // the accumulated bounds. Returns the number of free edge slots remaining, or
  // -1 if the path would overflow the buffer (nothing added — the caller should
  // flush() and retry into a fresh batch). Callers pass vec2_t; the font module
  // converts its compact glyph points before calling, so picovector stays
  // geometry-agnostic.
  int render_add_path(const vec2_t *pts, int count, mat3_t *transform) {
    if(count < 2) return MAX_EDGES - edge_count;            // no edges to add
    if(count > MAX_EDGES - edge_count) return -1;           // would overflow

    PV_T0(_t);
    PV_CNT(pv_paths, 1);
    PV_CNT(pv_edges, count);

    // Hoist the affine coefficients (no per-point method call).
    float a, b, c, d, e, f;
    if(transform) {
      a = transform->v00; c = transform->v01; e = transform->v02;
      b = transform->v10; d = transform->v11; f = transform->v12;
    } else {
      a = 1.0f; c = 0.0f; e = 0.0f; b = 0.0f; d = 1.0f; f = 0.0f;
    }

    // Transform single-core. Splitting this across both cores was tried and lost:
    // transform is memory-bandwidth bound (~1.5x ceiling, like build) and runs as
    // many small per-path batches, so the per-dispatch handshake overhead exceeds
    // the saving. (build wins from the same split only because it's one big batch.)
    int base = edge_count;
    float minx, miny, maxx, maxy;
    transform_points_range(pts, count, 0, count, base, a, b, c, d, e, f, minx, miny, maxx, maxy);
    edge_count = base + count;

    if(minx < acc_minx) acc_minx = minx;
    if(miny < acc_miny) acc_miny = miny;
    if(maxx > acc_maxx) acc_maxx = maxx;
    if(maxy > acc_maxy) acc_maxy = maxy;

    PV_ADD(pv_t_transform, _t);
    return MAX_EDGES - edge_count;
  }

  // Called once per frame (from image clear): prints the last sampled frame's
  // rasteriser stats roughly once a second, then resets the per-frame counters.
  void pv_profile_frame() {
#if PV_PROFILE
    uint64_t now = time_us_64();

    // Whole-frame time = clear()-to-clear() interval. This captures everything in
    // the frame (Python, input, vsync, present), not just the rasteriser phases,
    // so transform+build+raster < frame and the remainder is non-raster overhead.
    uint64_t frame_us = (pv_last_clear != 0) ? (now - pv_last_clear) : 0;
    pv_last_clear = now;

    if(now - pv_last_print >= 1000000) {
      // fps to one decimal without relying on %f (nano printf has no float support)
      unsigned long fps_x10 = frame_us ? (unsigned long)(10000000ull / frame_us) : 0;
      char buf[256];
      int n = snprintf(buf, sizeof(buf),
        "[pv] fps=%lu.%lu frame=%luus | transform=%luus build=%luus raster=%luus | paths=%lu edges=%lu pixels=%lu\n",
        fps_x10 / 10, fps_x10 % 10, (unsigned long)frame_us,
        (unsigned long)pv_t_transform, (unsigned long)pv_t_build, (unsigned long)pv_t_raster,
        (unsigned long)pv_paths, (unsigned long)pv_edges, (unsigned long)pv_pixels);
      mp_hal_stdout_tx_strn_cooked(buf, n);
      pv_last_print = now;
    }
    pv_paths = pv_edges = pv_pixels = 0;
    pv_t_transform = pv_t_build = pv_t_raster = 0;
#endif
  }

  int compare_nodes(const void* a, const void* b) {
    return *((int16_t*)a) - *((int16_t*)b);
  }

  // Emit a built tile's filled scanlines as opaque solid spans into the shared
  // span buffer (the hard-edged, non-antialiased path). The batch blend follows.
  static void emit_spans(int height, int sx, int sy, fill_rule_t fill_rule) {
    for(int y = 0; y < height; y++) {
      int n = node_count_buffer[y];        // crossings recorded by build_tile_nodes
      if(n == 0) continue;
      int16_t *nodes = &node_buffer[y * MAX_NODES_PER_SCANLINE];
      insertion_sort_i16(nodes, n);

      if(fill_rule == NON_ZERO) {
        int winding = 0, span_start = 0;
        for(int i = 0; i < n; i++) {
          int nx = nodes[i] >> 1;
          int prev = winding;
          winding += (nodes[i] & 1) ? -1 : 1;
          if(prev == 0 && winding != 0) span_start = nx;
          else if(prev != 0 && winding == 0 && span_start < nx) {
            PV_CNT(pv_pixels, nx - span_start);
            _add_span(sx + span_start, sy + y, nx - span_start);
          }
        }
      } else {
        for(int i = 0; i + 1 < n; i += 2) {
          int nsx = nodes[i] >> 1;
          int nex = nodes[i + 1] >> 1;
          if(nsx < nex) { PV_CNT(pv_pixels, nex - nsx); _add_span(sx + nsx, sy + y, nex - nsx); }
        }
      }
    }
  }

  // Build the scanline nodes for one tile from the accumulated edge buffer. The
  // mat3 was already folded in by add_path; here we just offset each edge into the
  // tile and clip + walk it (aa==0 hard-edge path, output resolution).
  static void build_tile_nodes(rect_t &tb) {
    vec2_t offset = tb.tl();
    for(int e = 0; e < edge_count; e++) {
      const edge_t &ed = edge_buffer[e];
      add_line_segment_to_nodes(vec2_t(ed.x0 - offset.x, ed.y0 - offset.y),
                                vec2_t(ed.x1 - offset.x, ed.y1 - offset.y), &tb);
    }
  }

#if PV_DUAL_CORE
  // One shared core1 worker serves two job kinds, both handed off through shared
  // memory (core0 bumps pv_go to dispatch, core1 sets pv_done when finished):
  //   KIND_PARALLEL_ROWS: core1 runs its parity half of an arbitrary per-row
  //          worker. This is how the rasteriser fold and image_t::blit reach
  //          core1 — the batch blend (_blend_spans) splits its span list this way.
  //   KIND_GENERIC_FN: core1 just runs gen_fn and reports done (the blur filter's
  //          core1 band, sharing this worker instead of launching its own).
  enum pv_kind_t { KIND_PARALLEL_ROWS = 2, KIND_GENERIC_FN = 3 };
  struct pv_fill_job_t {
    int kind;
    // generic parallel-rows job (KIND_PARALLEL_ROWS): each core runs its row half
    // of an arbitrary worker (the batch blend and image_t::blit)
    pv_row_worker_t row_fn;
    void *row_ctx;
    int row_y0, row_y1;
    // generic void() job (KIND_GENERIC_FN): core1 just runs gen_fn and reports
    // done. Used by the blur filter so it shares this one core1 worker instead of
    // launching a second, conflicting one.
    void (*gen_fn)();
  };
  static pv_fill_job_t pv_job;
  static volatile uint32_t pv_go = 0;        // core0 bumps to dispatch a job
  static volatile uint32_t pv_done = 0;      // core1 sets when its work is done
  static bool pv_core1_running = false;
  static uint32_t __attribute__((aligned(8))) pv_core1_stack[1024]; // 4kB core1 stack

  static void pv_core1_entry() {
    // The M33 FPU is per-core and a bare core1 launch leaves CP10/CP11 disabled,
    // so the float math in the build would UsageFault. Enable full access first.
    *(volatile uint32_t *)0xE000ED88 |= (0xF << 20); // CPACR: CP10/CP11 = full access
    __asm volatile("dsb");
    __asm volatile("isb");

    uint32_t served = 0;
    while(true) {
      while(pv_go == served) { __asm volatile("wfe"); } // sleep until a job (no bus contention)
      served = pv_go;
      __sync_synchronize();                  // observe pv_job (written before pv_go)

      // generic parallel-rows job: no build phase, no mid barrier — just run this
      // core's odd-parity row half and report done.
      if(pv_job.kind == KIND_PARALLEL_ROWS) {
        pv_job.row_fn(pv_job.row_ctx, pv_job.row_y0 + 1, pv_job.row_y1, 2);
        __sync_synchronize();
        pv_done = served;
        __asm volatile("sev");
        continue;
      }

      // generic void() job (e.g. the blur filter's core1 band): run it and report
      // done — no build, no barrier.
      if(pv_job.kind == KIND_GENERIC_FN) {
        if(pv_job.gen_fn) pv_job.gen_fn();
        __sync_synchronize();
        pv_done = served;
        __asm volatile("sev");
        continue;
      }

      // No other job kinds are dispatched (the rasteriser fold now drives core1
      // only through KIND_PARALLEL_ROWS, via _blend_spans/pv_parallel_rows), so
      // anything else just loops back to sleep.
    }
  }

  static void pv_core1_launch() {
    if(pv_core1_running) return;
    // MicroPython's lockout-victim FIFO IRQ on core0 would eat core1's launch
    // handshake, so gate it across the launch. We use shared memory at runtime,
    // so the IRQ can be restored afterwards (it simply never fires for us).
    irq_set_enabled(PV_SIO_FIFO_IRQ, false);
    multicore_launch_core1_with_stack(pv_core1_entry, pv_core1_stack, sizeof(pv_core1_stack));
    irq_set_enabled(PV_SIO_FIFO_IRQ, true);
    pv_core1_running = true;
  }

  // Split an arbitrary per-row worker across both cores by row parity. core1 runs
  // the odd-offset rows, core0 the even, then core0 blocks until core1 signals
  // done. Synchronous, and safe to call any time the rasteriser isn't mid-dispatch
  // (blits and render_flush never overlap — both fully drain core1 before
  // returning). ctx lives on the caller's stack, which stays valid because the
  // caller (core0) is parked in this function until the join.
  void pv_parallel_rows(pv_row_worker_t worker, void *ctx, int y0, int y1) {
    if(y1 - y0 < 2) { worker(ctx, y0, y1, 1); return; } // too small to split usefully

    pv_core1_launch();

    pv_job.kind = KIND_PARALLEL_ROWS;
    pv_job.row_fn = worker;
    pv_job.row_ctx = ctx;
    pv_job.row_y0 = y0;
    pv_job.row_y1 = y1;
    __sync_synchronize();                            // publish job before the go bump

    uint32_t ticket = pv_go + 1;
    pv_go = ticket; __asm volatile("sev");           // dispatch core1 (odd rows)

    worker(ctx, y0, y1, 2);                           // core0: even rows (y0, y0+2, …)

    while(pv_done != ticket) { __asm volatile("wfe"); } // join
    __sync_synchronize();                            // observe core1's writes
  }

  // Async single-job hand-off to the shared core1 worker, exposed to other
  // translation units (the blur filter). pv_core1_run() dispatches `fn` to core1
  // and returns immediately so the caller can do its own half in parallel;
  // pv_core1_join() then blocks until core1 has finished. Pair every run with a
  // join, and — like pv_parallel_rows — never overlap with a render_flush/blit
  // dispatch (they share pv_go/pv_done). extern "C" so blur.cpp can call it by
  // its plain, unmangled name.
  extern "C" void pv_core1_run(void (*fn)()) {
    pv_core1_launch();
    pv_job.kind = KIND_GENERIC_FN;
    pv_job.gen_fn = fn;
    __sync_synchronize();                            // publish job before the go bump
    pv_go = pv_go + 1; __asm volatile("sev");        // dispatch core1
  }
  extern "C" void pv_core1_join() {
    while(pv_done != pv_go) { __asm volatile("wfe"); }
    __sync_synchronize();                            // observe core1's writes
  }
#endif

  // Q11 fixed-point coverage accumulator (sa_acc, declared at file scope in the
  // working buffer): 11 fractional bits keep shallow-edge deposits from rounding
  // away, and the ~4 integer bits (range +/-16) cover any realistic winding.
  static const int   SA_ONE = 2048;        // Q11 fixed-point 1.0 (full coverage)
  static const float SA_SCALEF = 2048.0f;

  // Deposit one edge's signed area into `sa_acc` (row stride `w`, height `h`,
  // tile-local output-resolution coords). Each row the edge spans gets an "area"
  // term in the cell(s) the edge crosses plus a carry so the per-row prefix sum
  // fills everything to the right by the edge's signed vertical coverage. This is
  // the font-rs line-accumulation, with clamping so edges outside the tile's
  // x-range don't touch memory (a fully-left edge still carries full cover at x=0).
  static void signed_area_line(int w, int h, float x0, float y0, float x1, float y1) {
    if(y0 == y1) return;                       // horizontal: no vertical coverage
    float dir = 1.0f;
    if(y0 > y1) { dir = -1.0f; float t; t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    float dxdy = (x1 - x0) / (y1 - y0);
    if(y0 < 0.0f) { x0 += (0.0f - y0) * dxdy; y0 = 0.0f; } // clip to top
    if(y1 > (float)h) y1 = (float)h;                        // clip to bottom
    if(y0 >= y1) return;

    float x = x0;
    int yend = (int)ceilf(y1);
    for(int yi = (int)y0; yi < yend; yi++) {
      float dy = fminf((float)(yi + 1), y1) - fmaxf((float)yi, y0); // vertical span in this row
      float xnext = x + dxdy * dy;
      float d = dy * dir;
      int16_t *ln = &sa_acc[yi * w];
      // deposit v (a float coverage fraction) at column i as Q11, bounds-checked
      // (unsigned compare rejects i<0 and i>=w in one test)
      auto dep = [&](int i, float v) { if((unsigned)i < (unsigned)w) ln[i] += (int16_t)(v * SA_SCALEF); };

      float xa = x, xb = xnext;
      if(xa > xb) { float t = xa; xa = xb; xb = t; }
      if(xb <= 0.0f) { ln[0] += (int16_t)(d * SA_SCALEF); x = xnext; continue; } // wholly left -> full carry at x=0
      if(xa >= (float)w) { x = xnext; continue; }           // wholly right -> nothing in tile

      float x0floor = floorf(xa);
      int x0i = (int)x0floor;
      int x1i = (int)ceilf(xb);
      if(x1i <= x0i + 1) {
        // edge stays in one column this row: split area between it and the carry
        float xmf = 0.5f * (x + xnext) - x0floor;
        dep(x0i,     d * (1.0f - xmf));
        dep(x0i + 1, d * xmf);
      } else {
        float s = 1.0f / (xb - xa);
        float x0f = xa - x0floor;
        float a0 = 1.0f - x0f;                 // xa -> first cell boundary
        float x1f = xb - (float)x1i + 1.0f;    // xb's fraction into its (last) cell
        float am = 0.5f * s * a0 * a0;          // area in the first partial cell
        float tail = 0.5f * s * x1f * x1f;      // area in the last partial cell
        dep(x0i, d * am);
        if(x1i == x0i + 2) {
          dep(x0i + 1, d * (1.0f - am - tail));
        } else {
          float a1 = s * (1.5f - x0f);
          dep(x0i + 1, d * (a1 - am));
          for(int xi = x0i + 2; xi < x1i - 1; xi++) dep(xi, d * s);
          float a2 = a1 + (float)(x1i - x0i - 3) * s;
          dep(x1i - 1, d * (1.0f - a2 - tail));
        }
        dep(x1i, d * tail);
      }
      x = xnext;
    }
  }

  // Rasterise the antialiased tile with the signed-area method: clear the
  // accumulator, deposit every edge (device -> tile-local, output resolution),
  // then prefix-sum each row into coverage bytes and emit a masked span. No node
  // build, no supersample scaling. Coverage is analytic (256 levels).
  static void emit_sa_spans(int sx, int sy, int sw, int sh, fill_rule_t fill_rule) {
    int w = sw, h = sh;
    memset(sa_acc, 0, (size_t)(w * h) * sizeof(int16_t));

    vec2_t off((float)sx, (float)sy);
    for(int e = 0; e < edge_count; e++) {
      const edge_t &ed = edge_buffer[e];
      signed_area_line(w, h, ed.x0 - off.x, ed.y0 - off.y, ed.x1 - off.x, ed.y1 - off.y);
    }

    for(int y = 0; y < h; y++) {
      int16_t *arow = &sa_acc[y * w];
      uint8_t *cov = &tile_buffer[y * TILE_WIDTH];
      int32_t acc = 0;
      int minx = -1, maxx = -1;
      for(int x = 0; x < w; x++) {
        acc += arow[x];
        int32_t c = acc < 0 ? -acc : acc;         // |winding| in Q16
        // Fast path: coverage <= 1 (no self-overlap) - both fill rules agree.
        // Only overlapping winding (>1) needs the rule applied.
        if(c > SA_ONE) {
          if(fill_rule == NON_ZERO) {
            c = SA_ONE;
          } else {                                  // even-odd: integer triangle wave
            c &= (2 * SA_ONE - 1);                  // mod 2.0
            if(c > SA_ONE) c = 2 * SA_ONE - c;
          }
        }
        int a = (c + 4) >> 3;                       // Q11 -> 0..256, rounded
        if(a > 255) a = 255;
        cov[x] = (uint8_t)a;
        if(a) { if(minx < 0) minx = x; maxx = x; }
      }
      if(minx >= 0) _add_masked_span(sx + minx, sy + y, maxx - minx + 1, &cov[minx]);
    }
  }

  // Rasterise the whole accumulated batch, one screen-sized tile at a time.
  void render_flush(image_t *target, brush_t *brush) {
    if(edge_count == 0 || brush == nullptr) return;

    // device-space bounds of everything added, clipped to the target. Bail if
    // off-screen; the tile loop is tightened to the visible region.
    rect_t clip = target->clip();
    rect_t sb = rect_t(floorf(acc_minx), floorf(acc_miny),
                       ceilf(acc_maxx) - floorf(acc_minx),
                       ceilf(acc_maxy) - floorf(acc_miny));
    sb = sb.intersection(clip);
    if(sb.empty()) return;

    bool is_aa = target->antialias() != 0;
    fill_rule_t fill_rule = target->fill_rule();

    // A tile spans the whole lores screen (TILE_WIDTH x TILE_HEIGHT), so the common
    // case is a single tile / single pass; larger targets are tiled. The blend
    // splits itself across both cores when the batch is big enough (see
    // _blend_spans), so dual-core lives entirely inside the blend.
    int tile_h = TILE_HEIGHT;

    for(int y = sb.y; y < sb.y + sb.h; y += tile_h) {
      for(int x = sb.x; x < sb.x + sb.w; x += TILE_WIDTH) {
        rect_t tb = clip.intersection(rect_t(x, y, TILE_WIDTH, tile_h)).intersection(sb).round();
        if(tb.empty()) continue;

        int sx = tb.x, sy = tb.y, sw = tb.w, sh = tb.h;

        PV_T0(_t_raster);
        _reset_spans();
        if(is_aa) {
          // Analytic signed-area: deposit edges at output resolution, prefix-sum
          // each row to coverage, emit masked spans, then blend.
          emit_sa_spans(sx, sy, sw, sh, fill_rule);
          _blend_masked_spans(target, brush);
        } else {
          // Hard edges: build scanline crossings, emit solid spans, then blend.
          memset(node_count_buffer, 0, NODE_COUNT_BUFFER_SIZE);
          build_tile_nodes(tb);
          emit_spans(sh, sx, sy, fill_rule);
          _blend_spans(target, brush);
        }
        PV_ADD(pv_t_raster, _t_raster);
      }
    }
  }

  void render(shape_t *shape, image_t *target, mat3_t *transform, brush_t *brush) {
    if(shape->paths.empty()) return;

    // Cheap pre-transform cull: bounds() transforms only the 4 cached local-bbox
    // corners, so an off-screen shape can be skipped before we transform any of
    // its points. This is what lets `transform` fall as you zoom in and most
    // shapes leave the clip region.
    if(shape->bounds().round().intersection(target->clip()).empty()) return;

    // let the brush fold in the shape's transform (gradients track the shape)
    if(brush) brush->set_render_transform(transform);

    render_begin();
    for(auto &path : shape->paths) {
      // a path returning -1 exceeds the whole edge buffer and is skipped
      render_add_path(path.points.data(), (int)path.points.size(), transform);
    }
    render_flush(target, brush);
  }




















}
