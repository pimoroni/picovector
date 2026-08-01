#include <algorithm>
#include <cfloat>

// PV_PROFILE (rasteriser phase profiling) and PV_DUAL_CORE come from the picovector
// config — both default OFF (see config_default.hpp). The Badgeware/MicroPython
// build sets PV_DUAL_CORE=1. picovector.hpp is included first (via rasteriser.hpp)
// so those macros are defined before use below.
#include "rasteriser.hpp"

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

// One bit per accumulator cell, marking the columns an edge deposited into. The
// prefix sum still has to start at column 0 (that's where the backdrop winding
// lands), but once the running winding returns to zero a row cannot produce
// coverage again until the next deposit — so the scan jumps straight there
// instead of walking. Without it the scan costs the bounding box, which for a
// hollow shape (an arc, a ring, a stroked outline) is mostly empty: a 220px ring
// measured 5.3ms of bounding-box cost against 0.2ms of actual ink.
#define SA_BITS_STRIDE ((TILE_WIDTH + 31) / 32)                     // words per row
#define SA_BITS_SIZE   (SA_BITS_STRIDE * TILE_HEIGHT * (int)sizeof(uint32_t))
// Gap worth breaking a span for. Below this the two extra span setups in the
// blend cost more than walking the uncovered pixels.
#define SA_MIN_GAP 8

// Working-buffer layout. tile_buffer (coverage) and edge_buffer are used by both
// rasteriser paths. The node buffers (aa==0 path) and the signed-area accumulator
// sa_acc are mutually exclusive - a render_flush is either aa==0 (nodes) or aa>0
// (signed area) - so they overlay ONE shared region rather than each costing SRAM.
#define AA_REGION_OFF    (TILE_BUFFER_SIZE + EDGE_BUFFER_SIZE)
#define NODE_REGION_SIZE (NODE_BUFFER_SIZE + NODE_COUNT_BUFFER_SIZE)
#define SA_REGION_SIZE   (SA_ACC_SIZE + SA_BITS_SIZE)
#define AA_REGION_SIZE   (NODE_REGION_SIZE > SA_REGION_SIZE ? NODE_REGION_SIZE : SA_REGION_SIZE)
static_assert(AA_REGION_OFF + AA_REGION_SIZE <= PV_WORKING_BUFFER_SIZE,
              "PicoVector working buffer too small for a full-screen tile");

uint8_t  *tile_buffer       = (uint8_t *)&PicoVector_working_buffer[0];
edge_t   *edge_buffer       = (edge_t *)&PicoVector_working_buffer[TILE_BUFFER_SIZE];
int16_t  *node_buffer       = (int16_t *)&PicoVector_working_buffer[AA_REGION_OFF];
uint8_t  *node_count_buffer = (uint8_t *)&PicoVector_working_buffer[AA_REGION_OFF + NODE_BUFFER_SIZE];
int16_t  *sa_acc            = (int16_t *)&PicoVector_working_buffer[AA_REGION_OFF]; // shares the node region
uint32_t *sa_bits           = (uint32_t *)&PicoVector_working_buffer[AA_REGION_OFF + SA_ACC_SIZE];
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
// The signed-area path, split: clearing the accumulator, depositing the edges,
// the prefix-sum scan, and the blend. `raster` is their sum, which on its own
// can't say which of them a slow shape is spending its time in.
static uint64_t pv_t_clear = 0, pv_t_deposit = 0, pv_t_scan = 0, pv_t_blend = 0;
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
        "[pv] fps=%lu.%lu frame=%luus | transform=%luus build=%luus raster=%luus "
        "(clear=%lu deposit=%lu scan=%lu blend=%lu) | paths=%lu edges=%lu pixels=%lu\n",
        fps_x10 / 10, fps_x10 % 10, (unsigned long)frame_us,
        (unsigned long)pv_t_transform, (unsigned long)pv_t_build, (unsigned long)pv_t_raster,
        (unsigned long)pv_t_clear, (unsigned long)pv_t_deposit,
        (unsigned long)pv_t_scan, (unsigned long)pv_t_blend,
        (unsigned long)pv_paths, (unsigned long)pv_edges, (unsigned long)pv_pixels);
      mp_hal_stdout_tx_strn_cooked(buf, n);
      pv_last_print = now;
    }
    pv_paths = pv_edges = pv_pixels = 0;
    pv_t_transform = pv_t_build = pv_t_raster = 0;
    pv_t_clear = pv_t_deposit = pv_t_scan = pv_t_blend = 0;
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
      uint32_t *bits = &sa_bits[yi * SA_BITS_STRIDE];
      // Deposit v at column i, and mark the column so the scan knows to stop
      // there. Off-left columns (i<0) fold into column 0: the per-row prefix sum
      // starts there, so that column is the backdrop and must carry the full
      // winding from everything to its left (a shape running off the left edge).
      // Off-right columns (i>=w) have no pixels and no carry, so drop.
      auto dep = [&](int i, float v) {
        if(i < 0) i = 0;
        if(i < w) { ln[i] += (int16_t)(v * SA_SCALEF); bits[i >> 5] |= 1u << (i & 31); }
      };

      float xa = x, xb = xnext;
      if(xa > xb) { float t = xa; xa = xb; xb = t; }
      if(xb <= 0.0f) { ln[0] += (int16_t)(d * SA_SCALEF); bits[0] |= 1u; x = xnext; continue; } // wholly left
      if(xa >= (float)w) { x = xnext; continue; }           // wholly right

      float x0floor = floorf(xa);
      int x0i = (int)x0floor;
      int x1i = (int)ceilf(xb);
      if(x1i <= x0i + 1) {
        float xmf = 0.5f * (x + xnext) - x0floor;
        dep(x0i,     d * (1.0f - xmf));
        dep(x0i + 1, d * xmf);
      } else {
        float s = 1.0f / (xb - xa);
        float x0f = xa - x0floor;
        float a0 = 1.0f - x0f;
        float x1f = xb - (float)x1i + 1.0f;
        float am = 0.5f * s * a0 * a0;
        float tail = 0.5f * s * x1f * x1f;
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

  // Coverage byte for a winding. The fill rule is a template parameter because
  // it's constant for the whole flush and the M33 has no branch predictor - a
  // per-pixel branch on it costs a pipeline refill every time. The 0..256 result
  // is folded to 0..255 without a branch: 256 is the only value with bit 8 set.
  template<fill_rule_t RULE>
  static inline int sa_coverage(int32_t acc) {
    int32_t c = acc < 0 ? -acc : acc;             // |winding| in Q11
    // Fast path: coverage <= 1 (no self-overlap) - both fill rules agree.
    // Only overlapping winding (>1) needs the rule applied.
    if(c > SA_ONE) {
      if(RULE == NON_ZERO) {
        c = SA_ONE;
      } else {                                    // even-odd: integer triangle wave
        c &= (2 * SA_ONE - 1);                    // mod 2.0
        if(c > SA_ONE) c = 2 * SA_ONE - c;
      }
    }
    int a = (c + 4) >> 3;                         // Q11 -> 0..256, rounded
    return a - (a >> 8);
  }

  // First column at or after `x` that an edge deposited into, or -1 if the rest
  // of the row is untouched.
  static inline int sa_next_deposit(const uint32_t *bits, int x, int w) {
    int word = x >> 5;
    uint32_t m = bits[word] & (0xffffffffu << (x & 31));
    while(m == 0u) {
      if(++word >= SA_BITS_STRIDE) return -1;
      m = bits[word];
    }
    int next = (word << 5) + (int)__builtin_ctz(m);
    return next >= w ? -1 : next;
  }

  // Prefix-sum one row of the accumulator into coverage bytes and emit its spans.
  //
  // Only the columns an edge deposited into can change the winding, and sa_bits
  // marks exactly those. Everywhere between two of them the winding is constant,
  // so the whole run takes one coverage value: compute it once and memset the
  // run. That covers a shape's hole (coverage zero, so nothing is written and the
  // span breaks if the gap is worth it) and its solid interior (one value, which
  // for an opaque fill is 255) with the same code. What's left to walk a pixel at
  // a time is the antialiased fringe either side of an edge, which is the only
  // part that genuinely varies.
  template<fill_rule_t RULE>
  static void sa_scan_row(int16_t *arow, const uint32_t *bits, uint8_t *cov, int w,
                          int sx, int y, image_t *target, brush_t *brush) {
    int32_t acc = 0;
    int span_start = -1, span_end = -1;

    auto emit = [&]() {
      if(span_start < 0) return;
      if(_num_spans() >= PV_MASKED_SPAN_CAP) {   // buffer full: blend what we have
        _blend_masked_spans(target, brush);
        _reset_spans();
      }
      _add_masked_span(sx + span_start, y, span_end - span_start + 1, &cov[span_start]);
      span_start = -1;
    };

    int x = 0;
    while(x < w) {
      int next = sa_next_deposit(bits, x, w);
      int run_end = next < 0 ? w : next;
      if(run_end > x) {
        // A constant-winding run. Skipping it is exact: it carries no deposit, so
        // acc is unchanged when the scan resumes.
        int a = sa_coverage<RULE>(acc);
        if(a == 0) {
          if(span_start >= 0) {
            if(run_end - x >= SA_MIN_GAP) emit();
            else memset(&cov[x], 0, (size_t)(run_end - x)); // short gap stays in the span
          }
        } else {
          memset(&cov[x], a, (size_t)(run_end - x));
          if(span_start < 0) span_start = x;
          span_end = run_end - 1;
        }
        x = run_end;
        if(next < 0) break;                      // nothing further on this row
      }

      // A deposited column: the winding changes here, so this one is per-pixel.
      // Consecutive deposits - the antialiased fringe either side of an edge, and
      // any near-horizontal stretch of one - stay in this loop on a single bit
      // test, so the bitmap is only searched once per constant run.
      do {
        acc += arow[x];
        int a = sa_coverage<RULE>(acc);
        cov[x] = (uint8_t)a;
        if(a) { if(span_start < 0) span_start = x; span_end = x; }
        x++;
      } while(x < w && ((bits[x >> 5] >> (x & 31)) & 1u));
    }
    emit();
  }

  // Rasterise the antialiased tile with the signed-area method: clear the
  // accumulator, deposit every edge (device -> tile-local, output resolution),
  // then prefix-sum each row into coverage bytes and emit masked spans. No node
  // build, no supersample scaling. Coverage is analytic (256 levels).
  static void emit_sa_spans(int sx, int sy, int sw, int sh, fill_rule_t fill_rule,
                            image_t *target, brush_t *brush) {
    int w = sw, h = sh;
    PV_T0(_t_clear);
    memset(sa_acc, 0, (size_t)(w * h) * sizeof(int16_t));
    memset(sa_bits, 0, (size_t)h * SA_BITS_STRIDE * sizeof(uint32_t));
    PV_ADD(pv_t_clear, _t_clear);

    PV_T0(_t_deposit);
    vec2_t off((float)sx, (float)sy);
    float fw = (float)w, fh = (float)h;
    for(int e = 0; e < edge_count; e++) {
      const edge_t &ed = edge_buffer[e];
      float x0 = ed.x0 - off.x, y0 = ed.y0 - off.y;
      float x1 = ed.x1 - off.x, y1 = ed.y1 - off.y;
      // Every tile deposits from the whole batch, so at hires - where a shape
      // wider or taller than 160x120 is rasterised in two or four passes - most
      // edges are nothing to do with the tile in hand. Rejecting those here saves
      // the call, its divide, and its per-row walk.
      //
      // Only a miss above/below or wholly to the right can be dropped: an edge
      // off to the LEFT still carries winding that the row's first column has to
      // pick up as its backdrop.
      if((y0 <= 0.0f && y1 <= 0.0f) || (y0 >= fh && y1 >= fh)) continue;
      if(x0 >= fw && x1 >= fw) continue;
      signed_area_line(w, h, x0, y0, x1, y1);
    }
    PV_ADD(pv_t_deposit, _t_deposit);

    PV_T0(_t_scan);
    for(int y = 0; y < h; y++) {
      int16_t *arow = &sa_acc[y * w];
      const uint32_t *bits = &sa_bits[y * SA_BITS_STRIDE];
      uint8_t *cov = &tile_buffer[y * TILE_WIDTH];
      if(fill_rule == NON_ZERO) sa_scan_row<NON_ZERO>(arow, bits, cov, w, sx, sy + y, target, brush);
      else                      sa_scan_row<EVEN_ODD>(arow, bits, cov, w, sx, sy + y, target, brush);
    }
    PV_ADD(pv_t_scan, _t_scan);
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
          emit_sa_spans(sx, sy, sw, sh, fill_rule, target, brush);
          PV_T0(_t_blend);
          _blend_masked_spans(target, brush);
          PV_ADD(pv_t_blend, _t_blend);
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
