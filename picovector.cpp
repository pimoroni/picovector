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

#if PV_DUAL_CORE
// forward-declare the pico SDK symbols we use (avoids a hard dependency on the
// SDK include path; they're linked into the firmware). Work is handed to core1
// via shared memory, NOT the inter-core FIFO: MicroPython owns the FIFO (its
// lockout-victim IRQ on core0 would consume our messages). The FIFO is only used
// — with that IRQ briefly gated — by multicore_launch_core1 to start the core.
extern "C" {
  void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack_bottom, size_t stack_size_bytes);
  void irq_set_enabled(unsigned int num, bool enabled);
}
#define PV_SIO_FIFO_IRQ 25 // SIO_IRQ_FIFO on RP2350 (core0's FIFO IRQ)
#endif

using std::sort, std::min, std::max;

// The rasterisation memory pool (PicoVector_working_buffer) and its size now
// live in picovector_working_buffer.cpp so the size is configurable
// (PV_WORKING_BUFFER_SIZE) and the buffer can be shared with embedders that use
// it as scratch (e.g. PNG/JPEG decode in the MicroPython bindings). The extern
// declarations come in via picovector.hpp.

#define TILE_WIDTH 64
#define TILE_HEIGHT 64
#define MAX_NODES_PER_SCANLINE 64

// The dual-core build splits the edges in half: core0 writes its crossings into
// node rows [0, node_region), core1 into [node_region, 2*node_region), so the two
// cores never share a scanline counter; the fill/resolve then merges the regions.
// One tile at antialias `aa` needs (tile_h << aa) node rows per region, so two
// regions must fit the NODE_BUFFER_ROWS-row buffer — at 4x AA that forces
// half-height tiles. node_region is computed per-flush from the aa level.
#define NODE_BUFFER_ROWS (TILE_HEIGHT * 4) // 256 supersampled scanline rows

#define TILE_BUFFER_SIZE (TILE_WIDTH * (TILE_HEIGHT + 1) * sizeof(uint8_t)) // ~4kB tile buffer
#define NODE_BUFFER_ROW_SIZE (MAX_NODES_PER_SCANLINE * sizeof(int16_t))
#define NODE_BUFFER_SIZE (NODE_BUFFER_ROWS * NODE_BUFFER_ROW_SIZE) // 32kB node buffer
#define NODE_COUNT_BUFFER_SIZE (NODE_BUFFER_ROWS * sizeof(uint8_t)) // 256 byte node count buffer

// buffer that each tile is rendered into before callback
uint8_t *tile_buffer = (uint8_t *)&PicoVector_working_buffer[0];
int16_t *node_buffer = (int16_t *)&PicoVector_working_buffer[TILE_BUFFER_SIZE];
uint8_t *node_count_buffer = (uint8_t *)&PicoVector_working_buffer[TILE_BUFFER_SIZE + NODE_BUFFER_SIZE];

// edge accumulator for the retained renderer (begin / add_path / flush). Each
// path's points are mat3-transformed once into device-space edges here, then the
// flush rasterises the whole batch in one tile pass.
struct edge_t { float x0, y0, x1, y1; };
#define MAX_EDGES 1024 // 16kB; covers the worst single shape (world map max = 556 pts)
edge_t *edge_buffer = (edge_t *)&PicoVector_working_buffer[TILE_BUFFER_SIZE + NODE_BUFFER_SIZE + NODE_COUNT_BUFFER_SIZE];
static int   edge_count = 0;
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

  // row_base shifts this edge's crossings into a separate region of the node /
  // count buffers (0 for core0, TILE_HEIGHT for core1) so the two cores can each
  // build a disjoint half of the edges without racing the shared node counters.
  // The fill then merges the two regions per scanline.
  void add_line_segment_to_nodes(vec2_t start, vec2_t end, rect_t *tb, int row_base = 0) {
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
      int row = iy + row_base;

      // pack: x in the high bits, winding direction in bit 0 (tile-local x fits)
      node_buffer[(row * MAX_NODES_PER_SCANLINE) + node_count_buffer[row]] = (ix << 1) | dir_bit;
      node_count_buffer[row]++;

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

  uint8_t alpha_map_none[2] = {0, 255};
  uint8_t alpha_map_x4[5] = {0, 63, 127, 190, 255};
  uint8_t alpha_map_x16[17] = {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240, 255};

  // Resolve one output-row parity of an antialiased tile: accumulate sub-pixel
  // coverage for the output rows where (oy & 1) == parity, map coverage to alpha,
  // and blend them to the target. The node buffer is read-only here and the two
  // parities touch disjoint tile_buffer / framebuffer rows, so the two cores can
  // run parity 0 and 1 concurrently with no locking. parity < 0 does every row.
  static void resolve_aa_parity(rect_t *tb, uint aa, fill_rule_t fill_rule, int parity,
                                int sx, int sy, image_t *target, brush_t *brush,
                                masked_span_func_t fn, uint8_t *p_alpha_map, int node_region) {
    int ss = 1 << aa;       // supersample factor
    int mask = ss - 1;      // sub-pixel mask
    int minx = int(tb->w);  // tracked in supersampled x
    int maxx = 0;
    int out_miny = int(tb->h); // tracked in output rows
    int out_maxy = -1;
    int16_t merged[MAX_NODES_PER_SCANLINE * 2]; // scratch to combine the two build regions

    // --- coverage accumulation (this parity's output rows only) ---
    for(int y = 0; y < int(tb->h); y++) {
      int oy = y >> aa;
      if(parity >= 0 && (oy & 1) != parity) continue;

      // merge the two edge-half regions for this supersampled scanline (region 1
      // is empty when the build ran single-core, so we sort region 0 in place).
      int n0 = node_count_buffer[y];
      int n1 = node_count_buffer[y + node_region];
      int n = n0 + n1;
      if(n == 0) continue; // no nodes on this supersampled line

      int16_t *nodes;
      if(n1 == 0) {
        nodes = &node_buffer[y * MAX_NODES_PER_SCANLINE];
      } else {
        int16_t *r0 = &node_buffer[y * MAX_NODES_PER_SCANLINE];
        int16_t *r1 = &node_buffer[(y + node_region) * MAX_NODES_PER_SCANLINE];
        for(int i = 0; i < n0; i++) merged[i] = r0[i];
        for(int i = 0; i < n1; i++) merged[n0 + i] = r1[i];
        nodes = merged;
      }

      if(oy < out_miny) out_miny = oy;
      if(oy > out_maxy) out_maxy = oy;

      insertion_sort_i16(nodes, n);
      uint8_t *row_data = &tile_buffer[oy * TILE_WIDTH];

      // accumulate one filled span [spx, epx) of coverage into the output row,
      // distributing partial sub-pixel coverage at the two ends.
      auto fill_span = [&](int spx, int epx) {
        if(spx >= epx) return;
        if(spx < minx) minx = spx;
        if(epx > maxx) maxx = epx;
        int o0 = spx >> aa;
        int o1 = (epx - 1) >> aa;
        if(o0 == o1) {
          row_data[o0] += (epx - spx);
        } else {
          row_data[o0] += ss - (spx & mask);
          for(int ox = o0 + 1; ox < o1; ox++) row_data[ox] += ss;
          row_data[o1] += ((epx - 1) & mask) + 1;
        }
      };

      if(fill_rule == NON_ZERO) {
        int winding = 0, span_start = 0;
        for(int i = 0; i < n; i++) {
          int nx = nodes[i] >> 1;
          int prev = winding;
          winding += (nodes[i] & 1) ? -1 : 1;
          if(prev == 0 && winding != 0) span_start = nx;
          else if(prev != 0 && winding == 0) fill_span(span_start, nx);
        }
      } else {
        for(int i = 0; i + 1 < n; i += 2) fill_span(nodes[i] >> 1, nodes[i + 1] >> 1);
      }
    }

    if(out_maxy < 0) return; // no coverage for this parity

    int out_minx = (minx >> aa);
    int out_maxx = ((maxx + ss - 1) >> aa);
    int w = out_maxx - out_minx;
    if(w <= 0) return;

    // --- alpha-map + masked blend (this parity's output rows) ---
    int step = (parity < 0) ? 1 : 2;
    for(int oy = out_miny; oy <= out_maxy; oy += step) {
      uint8_t *p = &tile_buffer[oy * TILE_WIDTH + out_minx];
      for(int c = w; c--; p++) *p = p_alpha_map[*p]; // coverage -> alpha
      p = &tile_buffer[oy * TILE_WIDTH + out_minx];
      PV_CNT(pv_pixels, w);
      fn(target, brush, sx + out_minx, sy + oy, w, p);
    }
  }

  // Fill the aa==0 (no antialias) scanlines of a built tile, [y0, height)
  // stepping by `step` (1 = all rows; 2 = even/odd parity for the two cores).
  // node_region is the row offset of core1's edge-half (0 if built single-core).
  static void fill_aa0_rows(int y0, int height, int step, int sx, int sy,
                            image_t *target, brush_t *brush, span_func_t sfn, fill_rule_t fill_rule,
                            int node_region) {
    int16_t merged[MAX_NODES_PER_SCANLINE * 2]; // scratch when two regions must be combined

    for(int y = y0; y < height; y += step) {
      // The two cores build disjoint edge-halves into separate node regions
      // (core0 at row y, core1 at row y + node_region). Combine them. The
      // common single-core case has region 1 empty, so we sort region 0 in place.
      int n0 = node_count_buffer[y];
      int n1 = node_count_buffer[y + node_region];
      int n = n0 + n1;
      if(n == 0) continue;

      int16_t *nodes;
      if(n1 == 0) {
        nodes = &node_buffer[y * MAX_NODES_PER_SCANLINE];
      } else {
        int16_t *r0 = &node_buffer[y * MAX_NODES_PER_SCANLINE];
        int16_t *r1 = &node_buffer[(y + node_region) * MAX_NODES_PER_SCANLINE];
        for(int i = 0; i < n0; i++) merged[i] = r0[i];
        for(int i = 0; i < n1; i++) merged[n0 + i] = r1[i];
        nodes = merged;
      }
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
            sfn(target, brush, sx + span_start, sy + y, nx - span_start);
          }
        }
      } else {
        for(int i = 0; i + 1 < n; i += 2) {
          int nsx = nodes[i] >> 1;
          int nex = nodes[i + 1] >> 1;
          if(nsx < nex) { PV_CNT(pv_pixels, nex - nsx); sfn(target, brush, sx + nsx, sy + y, nex - nsx); }
        }
      }
    }
  }

  // Build the scanline nodes for one tile from a slice [e_start, e_end) of the
  // accumulated edge buffer. The mat3 was already folded in by add_path; here we
  // only apply the antialias scale and the tile offset, then clip + walk each
  // edge. row_base shifts the output into this core's node region (0 or
  // node_region) so the two cores can build disjoint edge halves without racing.
  static void build_tile_nodes_scaled(rect_t &tb, float scale, int e_start, int e_end, int row_base) {
    vec2_t offset = tb.tl();
    for(int e = e_start; e < e_end; e++) {
      const edge_t &ed = edge_buffer[e];
      vec2_t s(ed.x0 * scale - offset.x, ed.y0 * scale - offset.y);
      vec2_t en(ed.x1 * scale - offset.x, ed.y1 * scale - offset.y);
      add_line_segment_to_nodes(s, en, &tb, row_base);
    }
  }

#if PV_DUAL_CORE
  // Per tile, one dispatch covers both phases with an internal cross-core barrier
  // (cheaper than two dispatches — core1 only re-enters its wfe sleep once):
  //   build: core0 builds edges [0, half) into node region 0, core1 builds
  //          [half, edge_count) into region 1 (offset node_region). Every edge is
  //          processed once; separate regions keep the node counters race-free.
  //   <barrier> both builds must complete before either resolves (it reads both)
  //   resolve: core0 takes even output rows, core1 odd; each row merges the two
  //          regions. aa==0 emits opaque spans (KIND_RASTER0); aa>0 accumulates
  //          coverage + alpha-maps + blends (KIND_RESOLVE_AA).
  // Hand-off is shared-memory: core0 bumps pv_go to dispatch; the two pv_built
  // counters form the mid barrier; core1 sets pv_done when finished.
  enum pv_kind_t { KIND_RASTER0 = 0, KIND_RESOLVE_AA = 1 };
  struct pv_fill_job_t {
    int kind;
    rect_t tb;
    float scale;
    int e_start, e_end, row_base;
    int node_region; // row offset of core1's edge-half (for the resolve merge)
    int height, sx, sy;
    image_t *target;
    brush_t *brush;
    span_func_t sfn;
    fill_rule_t fill_rule;
    // antialiased resolve
    uint aa;
    masked_span_func_t mfn;
    uint8_t *alpha_map;
  };
  static pv_fill_job_t pv_job;
  static volatile uint32_t pv_go = 0;        // core0 bumps to dispatch a job
  static volatile uint32_t pv_built_0 = 0;   // core0 sets when its build half is done
  static volatile uint32_t pv_built_1 = 0;   // core1 sets when its build half is done
  static volatile uint32_t pv_done = 0;      // core1 sets when its fill is done
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

      // build this core's edge-half into region 1
      build_tile_nodes_scaled(pv_job.tb, pv_job.scale, pv_job.e_start, pv_job.e_end, pv_job.row_base);
      __sync_synchronize();
      pv_built_1 = served; __asm volatile("sev");          // signal build done
      while(pv_built_0 != served) { __asm volatile("wfe"); } // wait core0's build (barrier)
      __sync_synchronize();

      // resolve the odd output rows (merges both node regions)
      if(pv_job.kind == KIND_RESOLVE_AA) {
        resolve_aa_parity(&pv_job.tb, pv_job.aa, pv_job.fill_rule, 1, pv_job.sx, pv_job.sy,
                          pv_job.target, pv_job.brush, pv_job.mfn, pv_job.alpha_map, pv_job.node_region);
      } else {
        fill_aa0_rows(1, pv_job.height, 2, pv_job.sx, pv_job.sy,
                      pv_job.target, pv_job.brush, pv_job.sfn, pv_job.fill_rule, pv_job.node_region);
      }
      __sync_synchronize();
      pv_done = served;                      // signal completion
      __asm volatile("sev");                 // wake core0
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
#endif

  // Rasterise the whole accumulated batch in one tile pass.
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

    // antialias level of target image
    uint aa = (uint)target->antialias();

    uint8_t *p_alpha_map = alpha_map_none;
    if(aa == 1) p_alpha_map = alpha_map_x4;
    if(aa == 2) p_alpha_map = alpha_map_x16;

    // Span functions come from the brush we were handed (authoritative), not the
    // image's last-set pen — so callers can draw any shape with any brush without
    // first syncing it onto the image.
    masked_span_func_t fn = brush->masked_span_func();
    span_func_t sfn = brush->span_func(); // unmasked span for the aa == 0 fast path
    fill_rule_t fill_rule = target->fill_rule();

    // For the dual-core build split each core needs its own node region of
    // (tile_h << aa) rows; both regions must fit the NODE_BUFFER_ROWS-row buffer,
    // which at 4x AA forces half-height tiles. node_region is core1's row offset.
    int tile_h = TILE_HEIGHT;
    while((tile_h << aa) * 2 > NODE_BUFFER_ROWS) tile_h >>= 1;
    int node_region = tile_h << aa;

    bool is_aa = (aa != 0);

    // iterate over tiles
    for(int y = sb.y; y < sb.y + sb.h; y += tile_h) {
      for(int x = sb.x; x < sb.x + sb.w; x += TILE_WIDTH) {
        rect_t tb = clip.intersection(rect_t(x, y, TILE_WIDTH, tile_h)).intersection(sb).round();
        if(tb.empty()) { continue; } // if tile empty, skip it

        // screen coordinates for clipped tile (before antialias scaling)
        int sx = tb.x;
        int sy = tb.y;
        int sw = tb.w;
        int sh = tb.h;

        tb.x *= (1 << aa);
        tb.y *= (1 << aa);
        tb.w *= (1 << aa);
        tb.h *= (1 << aa);

        // clear node counts for both build regions
        memset(node_count_buffer, 0, NODE_COUNT_BUFFER_SIZE);

        // the aa == 0 fast path emits spans directly and never reads the tile
        // coverage buffer, so only bother clearing it when antialiasing
        if(is_aa) {
          for(int row = 0; row <= sh; ++row) {
            memset(&tile_buffer[row * TILE_WIDTH], 0, sw);
          }
        }

        // Both AA levels share one fused dispatch: the build splits the edges
        // across the two cores into disjoint node regions, and the resolve splits
        // the output rows by parity (merging the two regions per scanline). aa==0
        // emits opaque spans; aa>0 accumulates coverage, alpha-maps and blends.
#if PV_DUAL_CORE
        pv_core1_launch();
        int half = (edge_count + 1) / 2;
        pv_job.kind = is_aa ? KIND_RESOLVE_AA : KIND_RASTER0;
        pv_job.tb = tb; pv_job.scale = float(1 << aa);
        pv_job.e_start = half; pv_job.e_end = edge_count; pv_job.row_base = node_region;
        pv_job.node_region = node_region;
        pv_job.height = int(tb.h); pv_job.sx = sx; pv_job.sy = sy;
        pv_job.target = target; pv_job.brush = brush;
        pv_job.sfn = sfn; pv_job.mfn = fn; pv_job.fill_rule = fill_rule;
        pv_job.aa = aa; pv_job.alpha_map = p_alpha_map;
        __sync_synchronize();
        uint32_t ticket = pv_go + 1;
        pv_go = ticket; __asm volatile("sev");                                 // dispatch core1 (edge-half + odd rows)

        PV_T0(_t_build);
        build_tile_nodes_scaled(tb, float(1 << aa), 0, half, 0);               // core0: build [0, half) -> region 0
        __sync_synchronize();
        pv_built_0 = ticket; __asm volatile("sev");                            // signal build done
        while(pv_built_1 != ticket) { __asm volatile("wfe"); }                 // barrier: both halves built
        __sync_synchronize();
        PV_ADD(pv_t_build, _t_build);

        PV_T0(_t_raster);
        if(is_aa) resolve_aa_parity(&tb, aa, fill_rule, 0, sx, sy, target, brush, fn, p_alpha_map, node_region);
        else      fill_aa0_rows(0, int(tb.h), 2, sx, sy, target, brush, sfn, fill_rule, node_region); // core0: even rows
        while(pv_done != ticket) { __asm volatile("wfe"); }                    // wait for core1's odd rows
        PV_ADD(pv_t_raster, _t_raster);
#else
        PV_T0(_t_build);
        build_tile_nodes_scaled(tb, float(1 << aa), 0, edge_count, 0);         // single core, region 0 only
        PV_ADD(pv_t_build, _t_build);

        PV_T0(_t_raster);
        if(is_aa) resolve_aa_parity(&tb, aa, fill_rule, -1, sx, sy, target, brush, fn, p_alpha_map, node_region);
        else      fill_aa0_rows(0, int(tb.h), 1, sx, sy, target, brush, sfn, fill_rule, node_region);
        PV_ADD(pv_t_raster, _t_raster);
#endif
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
