// ============================================================================
// Alternative picovector rasteriser — signed-area / analytic coverage.
//
// This is a from-scratch, drop-in replacement for picovector.cpp implementing
// the same retained renderer API (render_begin / render_add_path / render_flush
// / render / pv_profile_frame) and owning PicoVector_working_buffer. Swap it in
// by replacing picovector.cpp with this file in picovector.cmake.
//
// Approach (deliberately different from the scanline+supersample rasteriser):
//   * Analytic coverage via a signed-area accumulation buffer (the technique
//     used by Raph Levien's font-rs and stb_truetype v2). For each edge we add
//     fractional "area" deltas to a per-tile float buffer; a horizontal prefix
//     sum over each row then yields the exact fractional coverage of every
//     pixel — perfect antialiasing at NATIVE resolution, with no supersampling
//     (so no 4x scanline blow-up for AA). The fill rule (even-odd / non-zero)
//     is applied to the prefix-summed winding value per pixel.
//   * Dual core: per tile, the edges are split in half — core0 accumulates its
//     half into buffer A, core1 the other half into buffer B (disjoint buffers,
//     no locking) — then, after a barrier, the fill is split by output-row
//     parity (core0 even rows, core1 odd), each row summing A+B. One fused
//     dispatch per tile. Single-core falls back to accumulate-all + fill-all.
//   * AA level only selects OFF (threshold coverage -> opaque spans) vs on
//     (emit per-pixel alpha); the analytic coverage is already exact, so X2/X4
//     are treated identically and look better than box supersampling.
// ============================================================================

#include <cmath>
#include <cstring>
#include <cfloat>

// Set to 1 to enable rasteriser phase profiling (prints phase timings to REPL).
#define PV_PROFILE 1
// Set to 1 to use core1 for rasterisation (always available on Badgeware).
#define PV_DUAL_CORE 1

#if PV_PROFILE
#include <cstdio>
#endif

#include "picovector.hpp"
#include "brush.hpp"
#include "image.hpp"
#include "shape.hpp"
#include "types.hpp"
#include "mat3.hpp"

#if PV_DUAL_CORE
extern "C" {
  void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack_bottom, size_t stack_size_bytes);
  void irq_set_enabled(unsigned int num, bool enabled);
}
#define PV_SIO_FIFO_IRQ 25 // SIO_IRQ_FIFO on RP2350 (core0's FIFO IRQ)
#endif

// 32-bit aligned scratch pool shared with the PNG/JPEG decoders (never used at
// the same time as rasterisation). Must keep this name + size: image_png/jpeg
// place their decoder state here.
char __attribute__((aligned(4))) PicoVector_working_buffer[working_buffer_size];

namespace picovector {

  // --- tile + buffer geometry ----------------------------------------------
  #define TILE_WIDTH  64
  #define TILE_HEIGHT 64
  // Each accumulation row carries two extra cells: the signed-area encoding can
  // write one column past the right edge, and clamped edges may land on column w.
  #define ACC_STRIDE  (TILE_WIDTH + 2)
  #define ACC_ROWS    TILE_HEIGHT
  #define ACC_CELLS   (ACC_ROWS * ACC_STRIDE)

  // Coverage is accumulated as int16 fixed-point (Q12: 4096 == full coverage).
  // Halving the cell from 4 to 2 bytes halves the per-tile clear and the fill's
  // read bandwidth, and lets the fill run in pure integer. Q12 keeps rounding
  // error (which only accrues at cells with an actual edge crossing) far below a
  // single alpha level, while leaving headroom for winding up to +-7 per cell.
  typedef int16_t acc_t;
  #define ACC_Q     4096
  #define ACC_QSHIFT 12

  // Two signed-area buffers (one per core) at the base of the working buffer,
  // then the transformed-edge accumulator after them.
  static acc_t *accA = (acc_t *)&PicoVector_working_buffer[0];
  static acc_t *accB = (acc_t *)&PicoVector_working_buffer[ACC_CELLS * sizeof(acc_t)];

  struct edge_t { float x0, y0, x1, y1; };
  #define MAX_EDGES 1024 // 16kB; covers the worst single shape
  static edge_t *edge_buffer =
    (edge_t *)&PicoVector_working_buffer[2 * ACC_CELLS * sizeof(acc_t)];
  static int   edge_count = 0;
  static float acc_minx, acc_miny, acc_maxx, acc_maxy; // running device-space bounds

  // Round a float coverage delta into the fixed-point accumulator.
  static inline void acc_add(acc_t *p, float v) {
    float q = v * (float)ACC_Q;
    *p += (acc_t)(q + (q >= 0.0f ? 0.5f : -0.5f));
  }

  // --- profiling (mirror of picovector.cpp so the two are comparable) -------
#if PV_PROFILE
  extern "C" uint64_t time_us_64(void);
  extern "C" void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len);
  static uint32_t pv_paths = 0, pv_edges = 0, pv_pixels = 0;
  static uint64_t pv_t_transform = 0, pv_t_build = 0, pv_t_raster = 0;
  static uint64_t pv_t_clear = 0, pv_t_wait = 0; // sub-splits of the accumulate phase
  static uint64_t pv_last_print = 0;
  static uint64_t pv_last_clear = 0;
  #define PV_T0(v)       uint64_t v = time_us_64()
  #define PV_ADD(acc, v) (acc) += time_us_64() - (v)
  #define PV_CNT(acc, n) (acc) += (n)
#else
  #define PV_T0(v)
  #define PV_ADD(acc, v)
  #define PV_CNT(acc, n)
#endif

  // ==========================================================================
  // signed-area accumulation
  // ==========================================================================

  // Accumulate one edge (tile-local coords; 0,0 = tile top-left) into `acc`.
  // After a horizontal prefix sum of each row, cell (x,y) holds the signed
  // fractional coverage contributed by this edge. w,h are the tile pixel extent.
  static void accumulate_edge(acc_t *acc, int h, int w,
                              float ex0, float ey0, float ex1, float ey1) {
    if(ey0 == ey1) return;                 // horizontal edge: no vertical coverage
    float dir = 1.0f;
    if(ey0 > ey1) {                        // orient downward, remember winding sign
      float t;
      t = ex0; ex0 = ex1; ex1 = t;
      t = ey0; ey0 = ey1; ey1 = t;
      dir = -1.0f;
    }
    if(ey1 <= 0.0f || ey0 >= (float)h) return; // fully above/below the tile

    float dxdy = (ex1 - ex0) / (ey1 - ey0);
    float ytop = ey0 < 0.0f ? 0.0f : ey0;
    float ybot = ey1 > (float)h ? (float)h : ey1;

    int ys = (int)ytop;                    // ytop >= 0 so (int) floors
    int ye = (int)ybot; if((float)ye < ybot) ye++; // ceil(ybot) without a libm call
    float xcur = ex0 + (ytop - ey0) * dxdy; // edge x at the top of the current row (DDA-carried)
    for(int y = ys; y < ye; y++) {
      float ryt = (y == ys) ? ytop : (float)y;                   // segment span in this row
      float ryb = (float)(y + 1); if(ryb > ybot) ryb = ybot;
      float dy = ryb - ryt;
      float xa = xcur;                     // x at ryt (carried, no recompute)
      float xb = xcur + dxdy * dy;          // x at ryb
      xcur = xb;                            // becomes the next row's top x
      if(dy <= 0.0f) continue;             // degenerate row (rare float boundary)
      // Clamp x into [0,w]: in-tile coverage is unchanged (an edge left of the
      // tile fully covers to its right; one to the right contributes nothing).
      if(xa < 0.0f) xa = 0.0f; else if(xa > (float)w) xa = (float)w;
      if(xb < 0.0f) xb = 0.0f; else if(xb > (float)w) xb = (float)w;

      float d  = dir * dy;                 // signed vertical coverage of this segment
      float x0 = xa < xb ? xa : xb;
      float x1 = xa < xb ? xb : xa;
      acc_t *row = acc + y * (w + 2);      // rows packed tightly at stride w+2

      int x0i = (int)x0;                    // x0 >= 0 -> floor
      int x1i = (int)x1; if((float)x1i < x1) x1i++; // ceil(x1) without a libm call
      if(x1i <= x0i + 1) {
        // segment within a single column: split d by the mid-point fraction
        float xmf = 0.5f * (x0 + x1) - (float)x0i;
        acc_add(&row[x0i],     d * (1.0f - xmf));
        acc_add(&row[x0i + 1], d * xmf);
      } else {
        // segment spans several columns: distribute the trapezoidal area
        float s   = 1.0f / (x1 - x0);
        float x0f = x0 - (float)x0i;
        float a0  = 1.0f - x0f;
        float x1f = x1 - (float)(x1i - 1);
        float am0 = 0.5f * s * a0 * a0;
        acc_add(&row[x0i], d * am0);
        if(x1i == x0i + 2) {
          acc_add(&row[x0i + 1], d * (1.0f - am0 - 0.5f * s * x1f * x1f));
        } else {
          float a1 = s * (1.5f - x0f);
          acc_add(&row[x0i + 1], d * (a1 - am0));
          for(int xi = x0i + 2; xi < x1i - 1; xi++) acc_add(&row[xi], d * s);
          float a2 = a1 + (float)(x1i - x0i - 3) * s;
          acc_add(&row[x1i - 1], d * (1.0f - a2 - 0.5f * s * x1f * x1f));
        }
        acc_add(&row[x1i], d * (0.5f * s * x1f * x1f));
      }
    }
  }

  // Rows are packed tightly (stride = w+2) for the current tile, so the whole
  // used region clears in ONE memset of exactly the needed bytes — minimising
  // both per-call overhead (vs h per-row memsets) and bytes (vs a fixed stride).
  static inline void clear_acc_rows(acc_t *acc, int h, int w) {
    memset(acc, 0, (size_t)h * (w + 2) * sizeof(acc_t));
  }

  // Fill one output row: prefix-sum A+B to coverage (pure integer fixed-point),
  // apply the fill rule, and emit either an opaque span (aa off) or an
  // alpha-masked span (aa on).
  static void fill_row(int y, int w, int sx, int sy, bool aa, bool even_odd,
                       image_t *target, brush_t *brush, span_func_t sfn, masked_span_func_t mfn) {
    int stride = w + 2;                    // rows packed tightly (matches accumulate/clear)
    acc_t *ra = accA + y * stride;
    acc_t *rb = accB + y * stride;
    int32_t acc = 0;

    if(!aa) {
      int run = -1;
      for(int x = 0; x < w; x++) {
        acc += (int)ra[x] + (int)rb[x];
        int c = acc < 0 ? -acc : acc;
        if(even_odd) { c &= (2 * ACC_Q - 1); if(c > ACC_Q) c = 2 * ACC_Q - c; } // fold to triangle
        bool inside = c >= (ACC_Q / 2);
        if(inside) { if(run < 0) run = x; }
        else if(run >= 0) { sfn(target, brush, sx + run, sy + y, x - run); PV_CNT(pv_pixels, x - run); run = -1; }
      }
      if(run >= 0) { sfn(target, brush, sx + run, sy + y, w - run); PV_CNT(pv_pixels, w - run); }
    } else {
      uint8_t alpha[TILE_WIDTH];
      int x0 = -1, x1 = 0;
      for(int x = 0; x < w; x++) {
        acc += (int)ra[x] + (int)rb[x];
        int c = acc < 0 ? -acc : acc;
        if(even_odd) { c &= (2 * ACC_Q - 1); if(c > ACC_Q) c = 2 * ACC_Q - c; }
        else if(c > ACC_Q) c = ACC_Q;
        int a = (c * 255) >> ACC_QSHIFT;  // c in [0,ACC_Q] -> [0,255]
        if(a > 255) a = 255;
        alpha[x] = (uint8_t)a;
        if(a) { if(x0 < 0) x0 = x; x1 = x; }
      }
      if(x0 >= 0) { mfn(target, brush, sx + x0, sy + y, x1 - x0 + 1, alpha + x0); PV_CNT(pv_pixels, x1 - x0 + 1); }
    }
  }

  // ==========================================================================
  // dual-core handshake (shared-memory wfe/sev, no FIFO)
  // ==========================================================================
#if PV_DUAL_CORE
  struct pv_job_t {
    int sx, sy, w, h;
    int e_half, e_count;   // core1 accumulates edges [e_half, e_count) into accB
    bool aa, even_odd;
    image_t *target; brush_t *brush;
    span_func_t sfn; masked_span_func_t mfn;
  };
  static pv_job_t pv_job;
  static volatile uint32_t pv_go = 0;
  static volatile uint32_t pv_built_0 = 0; // core0 accumulate done
  static volatile uint32_t pv_built_1 = 0; // core1 accumulate done
  static volatile uint32_t pv_done = 0;    // core1 fill done
  static bool pv_core1_running = false;
  static uint32_t __attribute__((aligned(8))) pv_core1_stack[1024];

  static void pv_core1_entry() {
    *(volatile uint32_t *)0xE000ED88 |= (0xF << 20); // enable this core's FPU (CP10/CP11)
    __asm volatile("dsb"); __asm volatile("isb");

    uint32_t served = 0;
    while(true) {
      while(pv_go == served) { __asm volatile("wfe"); }
      served = pv_go;
      __sync_synchronize();

      int w = pv_job.w, h = pv_job.h, sx = pv_job.sx, sy = pv_job.sy;
      clear_acc_rows(accB, h, w);
      for(int e = pv_job.e_half; e < pv_job.e_count; e++) {
        edge_t &ed = edge_buffer[e];
        accumulate_edge(accB, h, w, ed.x0 - sx, ed.y0 - sy, ed.x1 - sx, ed.y1 - sy);
      }
      __sync_synchronize();
      pv_built_1 = served; __asm volatile("sev");
      while(pv_built_0 != served) { __asm volatile("wfe"); } // barrier: both halves accumulated
      __sync_synchronize();

      for(int y = 1; y < h; y += 2) // odd output rows
        fill_row(y, w, sx, sy, pv_job.aa, pv_job.even_odd, pv_job.target, pv_job.brush, pv_job.sfn, pv_job.mfn);
      __sync_synchronize();
      pv_done = served; __asm volatile("sev");
    }
  }

  static void pv_core1_launch() {
    if(pv_core1_running) return;
    irq_set_enabled(PV_SIO_FIFO_IRQ, false);
    multicore_launch_core1_with_stack(pv_core1_entry, pv_core1_stack, sizeof(pv_core1_stack));
    irq_set_enabled(PV_SIO_FIFO_IRQ, true);
    pv_core1_running = true;
  }
#endif

  // ==========================================================================
  // retained renderer API
  // ==========================================================================

  void render_begin() {
    edge_count = 0;
    acc_minx = acc_miny = FLT_MAX;
    acc_maxx = acc_maxy = -FLT_MAX;
  }

  int render_add_path(const vec2_t *pts, int count, mat3_t *transform) {
    if(count < 2) return MAX_EDGES - edge_count;
    if(count > MAX_EDGES - edge_count) return -1;

    PV_T0(_t);
    PV_CNT(pv_paths, 1);
    PV_CNT(pv_edges, count);

    float a, b, c, d, e, f;
    if(transform) {
      a = transform->v00; c = transform->v01; e = transform->v02;
      b = transform->v10; d = transform->v11; f = transform->v12;
    } else {
      a = 1.0f; c = 0.0f; e = 0.0f; b = 0.0f; d = 1.0f; f = 0.0f;
    }

    int base = edge_count;
    const vec2_t &lp = pts[count - 1];               // close the loop
    float px = a * lp.x + c * lp.y + e;
    float py = b * lp.x + d * lp.y + f;
    for(int i = 0; i < count; i++) {
      float cx = a * pts[i].x + c * pts[i].y + e;
      float cy = b * pts[i].x + d * pts[i].y + f;
      edge_buffer[base + i] = { px, py, cx, cy };
      if(cx < acc_minx) acc_minx = cx;
      if(cx > acc_maxx) acc_maxx = cx;
      if(cy < acc_miny) acc_miny = cy;
      if(cy > acc_maxy) acc_maxy = cy;
      px = cx; py = cy;
    }
    edge_count = base + count;

    PV_ADD(pv_t_transform, _t);
    return MAX_EDGES - edge_count;
  }

  void render_flush(image_t *target, brush_t *brush) {
    if(edge_count == 0) return;

    rect_t clip = target->clip();
    rect_t sb = rect_t(floorf(acc_minx), floorf(acc_miny),
                       ceilf(acc_maxx) - floorf(acc_minx),
                       ceilf(acc_maxy) - floorf(acc_miny));
    sb = sb.intersection(clip);
    if(sb.empty()) return;

    bool aa       = (target->antialias() != OFF);
    bool even_odd = (target->fill_rule() == EVEN_ODD);
    span_func_t        sfn = target->_span_func;
    masked_span_func_t mfn = target->_masked_span_func;

#if !PV_DUAL_CORE
    memset(accB, 0, ACC_CELLS * sizeof(acc_t)); // single core never writes accB
#endif

    int sbx = (int)sb.x, sby = (int)sb.y, sbw = (int)sb.w, sbh = (int)sb.h;
    for(int ty = sby; ty < sby + sbh; ty += TILE_HEIGHT) {
      for(int tx = sbx; tx < sbx + sbw; tx += TILE_WIDTH) {
        rect_t tbr = clip.intersection(rect_t(tx, ty, TILE_WIDTH, TILE_HEIGHT)).intersection(sb).round();
        if(tbr.empty()) continue;

        int sx = (int)tbr.x, sy = (int)tbr.y, w = (int)tbr.w, h = (int)tbr.h;
        if(w > TILE_WIDTH)  w = TILE_WIDTH;
        if(h > TILE_HEIGHT) h = TILE_HEIGHT;

        int half = (edge_count + 1) / 2;

#if PV_DUAL_CORE
        pv_core1_launch();
        pv_job.sx = sx; pv_job.sy = sy; pv_job.w = w; pv_job.h = h;
        pv_job.e_half = half; pv_job.e_count = edge_count;
        pv_job.aa = aa; pv_job.even_odd = even_odd;
        pv_job.target = target; pv_job.brush = brush; pv_job.sfn = sfn; pv_job.mfn = mfn;
        __sync_synchronize();
        uint32_t ticket = pv_go + 1;
        pv_go = ticket; __asm volatile("sev");                 // core1: accumulate [half,n) + fill odd rows

        PV_T0(_t_build);
        PV_T0(_t_clr);
        clear_acc_rows(accA, h, w);
        PV_ADD(pv_t_clear, _t_clr);
        for(int ei = 0; ei < half; ei++) {                     // core0: accumulate [0, half) into accA
          edge_t &ed = edge_buffer[ei];
          accumulate_edge(accA, h, w, ed.x0 - sx, ed.y0 - sy, ed.x1 - sx, ed.y1 - sy);
        }
        __sync_synchronize();
        pv_built_0 = ticket; __asm volatile("sev");
        PV_T0(_t_w);
        while(pv_built_1 != ticket) { __asm volatile("wfe"); } // barrier
        PV_ADD(pv_t_wait, _t_w);
        __sync_synchronize();
        PV_ADD(pv_t_build, _t_build);

        PV_T0(_t_raster);
        for(int y = 0; y < h; y += 2)                          // core0: even output rows
          fill_row(y, w, sx, sy, aa, even_odd, target, brush, sfn, mfn);
        while(pv_done != ticket) { __asm volatile("wfe"); }
        PV_ADD(pv_t_raster, _t_raster);
#else
        PV_T0(_t_build);
        clear_acc_rows(accA, h, w);
        for(int ei = 0; ei < edge_count; ei++) {
          edge_t &ed = edge_buffer[ei];
          accumulate_edge(accA, h, w, ed.x0 - sx, ed.y0 - sy, ed.x1 - sx, ed.y1 - sy);
        }
        PV_ADD(pv_t_build, _t_build);

        PV_T0(_t_raster);
        for(int y = 0; y < h; y++)
          fill_row(y, w, sx, sy, aa, even_odd, target, brush, sfn, mfn);
        PV_ADD(pv_t_raster, _t_raster);
#endif
      }
    }
  }

  void render(shape_t *shape, image_t *target, mat3_t *transform, brush_t *brush) {
    if(shape->paths.empty()) return;
    if(shape->bounds().round().intersection(target->clip()).empty()) return; // pre-transform cull

    render_begin();
    for(auto &path : shape->paths)
      render_add_path(path.points.data(), (int)path.points.size(), transform);
    render_flush(target, brush);
  }

  void pv_profile_frame() {
#if PV_PROFILE
    uint64_t now = time_us_64();
    uint64_t frame_us = (pv_last_clear != 0) ? (now - pv_last_clear) : 0;
    pv_last_clear = now;

    if(now - pv_last_print >= 1000000) {
      unsigned long fps_x10 = frame_us ? (unsigned long)(10000000ull / frame_us) : 0;
      char buf[256];
      int n = snprintf(buf, sizeof(buf),
        "[pv2] fps=%lu.%lu frame=%luus | transform=%luus accum=%luus (clear=%luus wait=%luus) fill=%luus | px=%lu\n",
        fps_x10 / 10, fps_x10 % 10, (unsigned long)frame_us,
        (unsigned long)pv_t_transform, (unsigned long)pv_t_build,
        (unsigned long)pv_t_clear, (unsigned long)pv_t_wait, (unsigned long)pv_t_raster,
        (unsigned long)pv_pixels);
      mp_hal_stdout_tx_strn_cooked(buf, n);
      pv_last_print = now;
    }
    pv_paths = pv_edges = pv_pixels = 0;
    pv_t_transform = pv_t_build = pv_t_raster = 0;
    pv_t_clear = pv_t_wait = 0;
#endif
  }

}
